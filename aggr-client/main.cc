#include <iostream>
#include <vector>

#include <signal.h>

#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>

// define some macros as configuration parameters
#define BATCH_SIZE 32

#define NB_MBUF 8192
#define MEMPOOL_CACHE_SIZE 256

#define PAYLOAD_LEN 1450
#define MAX_MTU 1518

#define SUBFLOW_MAGIC 0x12ABCDEF

#define PORT_ID 0

// define a global variable for quitting.
static volatile bool force_quit = false;

// a static function defined for signal handling
static void signal_handler(int signum)
{
	if (signum == SIGINT || signum == SIGTERM)
	{
		printf("\n\nSignal %d received, preparing to exit...\n",
			   signum);
		force_quit = true;
	}
}

struct subflow_header
{
	uint32_t magic_num;
	uint32_t subflow_idx;
	uint64_t seq_num;
} __attribute__((__packed__));

class subflow_header_generator
{
public:
	subflow_header_generator() : _counter(0) {}

	subflow_header gen_header()
	{
		subflow_header header;

		uint32_t curr_idx = _counter % 5;
		header.magic_num = SUBFLOW_MAGIC;
		header.seq_num = _counter;
		if (curr_idx < 3)
		{
			header.subflow_idx = curr_idx;
		}
		else
		{
			header.subflow_idx = curr_idx - 3;
		}

		_counter += 1;
		return header;
	}

	uint64_t _counter;
};

class payload_generator
{
public:
	payload_generator(struct rte_mempool *mpool, std::vector<uint8_t> payload_template) : _payload_template(std::move(payload_template)), _mpool(mpool) {}

	void gen_payload(std::vector<struct rte_mbuf *> &batch)
	{
		// allocate mbufs into the batch vector
		int ret = rte_mempool_get_bulk(_mpool, (void **)batch.data(), batch.size());
		assert(ret == 0);

		// iterate through each rte_mbuf and fill the content
		for (int i = 0; i < batch.size(); i++)
		{
			struct rte_mbuf *mbuf = batch[i];

			rte_pktmbuf_reset(mbuf);
			mbuf->data_len = _payload_template.size();
			mbuf->pkt_len = _payload_template.size();

			rte_memcpy(rte_pktmbuf_mtod(mbuf, char *), (char *)_payload_template.data(), _payload_template.size());
		}
	}

	std::vector<uint8_t> _payload_template;
	struct rte_mempool *_mpool;
};

int main(int argc, char **argv)
{
	assert(sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_udp_hdr) + sizeof(subflow_header) < RTE_PKTMBUF_HEADROOM);
	assert(sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_udp_hdr) + sizeof(subflow_header) + PAYLOAD_LEN <= MAX_MTU);

	// init EAL
	int ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Invalid EAL arguments\n");
	argc -= ret;
	argv += ret;

	// register signal handler
	force_quit = false;
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	// print the socket id
	std::cout << "The thread is running on socket " << rte_socket_id() << std::endl;

	// allocate the mempool
	struct rte_mempool *mpool = rte_pktmbuf_pool_create("mbuf_pool", NB_MBUF,
														MEMPOOL_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE,
														rte_socket_id());
	if (mpool == NULL)
		rte_exit(EXIT_FAILURE, "Cannot init mbuf pool\n");
	else
		std::cout << "Finish creating packet memory buffer pool." << std::endl;

	// prepare the batch
	std::vector<struct rte_mbuf *> batch(BATCH_SIZE, nullptr);
	assert(batch.size() == BATCH_SIZE);

	// prepare the payload generator
	std::vector<uint8_t> payload_template(PAYLOAD_LEN, 0xfe);
	payload_generator payload_gen = payload_generator(mpool, std::move(payload_template));

	// prepare the subflow header generator
	subflow_header_generator sfheader_gen = subflow_header_generator();

	// prepare the udp headers
	std::vector<struct rte_udp_hdr> udp_hdrs;
	struct rte_udp_hdr udp_hdr;
	udp_hdr.src_port = rte_cpu_to_be_16(1024);
	udp_hdr.dst_port = rte_cpu_to_be_16(250);
	udp_hdr.dgram_len = rte_cpu_to_be_16(sizeof(struct rte_udp_hdr) + sizeof(subflow_header) + PAYLOAD_LEN);
	udp_hdr.dgram_cksum = 0;
	udp_hdrs.push_back(udp_hdr);

	// prepare the ethernet headers
	uint8_t smac[6] = {0x0c, 0x42, 0xa1, 0x3a, 0x67, 0x38};
	uint8_t dmac[6] = {0x08, 0x68, 0x8d, 0x61, 0x76, 0x84};
	std::vector<struct rte_ether_hdr> eth_hdrs;
	struct rte_ether_hdr eth_hdr;
	rte_ether_addr_copy((rte_ether_addr *)smac, &eth_hdr.s_addr);
	rte_ether_addr_copy((rte_ether_addr *)dmac, &eth_hdr.d_addr);
	eth_hdr.ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);
	eth_hdrs.push_back(eth_hdr);

	// prepare the ipv4 addrs
	uint32_t sip = (198U << 24) | (18 << 16) | (0 << 8) | 1;
	uint32_t dip1 = (192U << 24) | (168 << 16) | (81 << 8) | 2;
	uint32_t dip2 = (192U << 24) | (168 << 16) | (82 << 8) | 3;
	uint32_t dip3 = (192U << 24) | (168 << 16) | (83 << 8) | 4;
	std::vector<uint32_t> ips;
	ips.push_back(dip1);
	ips.push_back(dip2);
	ips.push_back(dip3);

	// prepare the ipv4 hdrs
	std::vector<struct rte_ipv4_hdr> ip_hdrs;
	for (int i = 0; i < 3; i++)
	{
		struct rte_ipv4_hdr ip_hdr;

		ip_hdr.version_ihl = RTE_IPV4_VHL_DEF;
		ip_hdr.type_of_service = 0;
		ip_hdr.fragment_offset = 0;
		ip_hdr.time_to_live = 64;
		ip_hdr.next_proto_id = IPPROTO_UDP;
		ip_hdr.packet_id = 0;
		ip_hdr.total_length = rte_cpu_to_be_16(sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_udp_hdr) + sizeof(subflow_header) + PAYLOAD_LEN);
		ip_hdr.src_addr = rte_cpu_to_be_32(sip);
		ip_hdr.dst_addr = rte_cpu_to_be_32(ips[i]);

		uint16_t *ptr16 = (uint16_t *)(&ip_hdr);
		uint32_t ip_cksum = 0;
		ip_cksum += ptr16[0];
		ip_cksum += ptr16[1];
		ip_cksum += ptr16[2];
		ip_cksum += ptr16[3];
		ip_cksum += ptr16[4];
		ip_cksum += ptr16[6];
		ip_cksum += ptr16[7];
		ip_cksum += ptr16[8];
		ip_cksum += ptr16[9];
		ip_cksum = ((ip_cksum & 0xFFFF0000) >> 16) +
				   (ip_cksum & 0x0000FFFF);
		if (ip_cksum > 65535)
			ip_cksum -= 65535;
		ip_cksum = (~ip_cksum) & 0x0000FFFF;
		if (ip_cksum == 0)
			ip_cksum = 0xFFFF;
		ip_hdr.hdr_checksum = (uint16_t)ip_cksum;

		ip_hdrs.push_back(ip_hdr);
	}

	// initialize the port
	struct rte_eth_conf port_conf = {
		.rxmode = {
			.split_hdr_size = 0,
		},
		.txmode = {
			.mq_mode = ETH_MQ_TX_NONE,
		},
	};

	// get the number of port and check that the port id is valid
	int nb_ports = rte_eth_dev_count_avail();
	if (nb_ports == 0)
	{
		rte_exit(EXIT_FAILURE, "No Ethernet ports - bye\n");
	}
	assert(PORT_ID < nb_ports);

	// get the dev info
	struct rte_eth_dev_info dev_info;
	ret = rte_eth_dev_info_get(PORT_ID, &dev_info);
	if (ret != 0)
	{
		rte_exit(EXIT_FAILURE,
				 "Error during getting device (port %u) info: %s\n",
				 PORT_ID, strerror(-ret));
	}

	// check that the port and the thread are on the same socket
	assert(rte_socket_id() == rte_eth_dev_socket_id(PORT_ID));
	std::cout << "port " << PORT_ID << " runs on socket " << rte_eth_dev_socket_id(PORT_ID) << std::endl;

	// initialize the port
	ret = rte_eth_dev_configure(PORT_ID, 1, 1, &port_conf);
	if (ret < 0)
	{
		rte_exit(EXIT_FAILURE,
				 "Cannot adjust number of descriptors: err=%d, port=%u\n",
				 ret, PORT_ID);
	}

	// adjust the descriptor
	uint16_t ndesc = 256;
	ret = rte_eth_dev_adjust_nb_rx_tx_desc(PORT_ID, &ndesc, &ndesc);
	if (ret < 0)
	{
		rte_exit(EXIT_FAILURE,
				 "Cannot adjust number of descriptors: err=%d, port=%u\n",
				 ret, PORT_ID);
	}

	// init rx queue
	struct rte_eth_rxconf rxq_conf = dev_info.default_rxconf;
	rxq_conf.offloads = port_conf.rxmode.offloads;
	ret = rte_eth_rx_queue_setup(PORT_ID, 0, ndesc,
								 rte_eth_dev_socket_id(PORT_ID),
								 &rxq_conf,
								 mpool);
	if (ret < 0)
	{
		rte_exit(EXIT_FAILURE, "rte_eth_rx_queue_setup:err=%d, port=%u\n",
				 ret, PORT_ID);
	}

	// init tx queue
	struct rte_eth_txconf txq_conf = dev_info.default_txconf;
	txq_conf.offloads = port_conf.txmode.offloads;
	ret = rte_eth_tx_queue_setup(PORT_ID, 0, ndesc,
								 rte_eth_dev_socket_id(PORT_ID),
								 &txq_conf);
	if (ret < 0)
	{
		rte_exit(EXIT_FAILURE, "rte_eth_tx_queue_setup:err=%d, port=%u\n",
				 ret, PORT_ID);
	}

	/* Start device */
	ret = rte_eth_dev_start(PORT_ID);
	if (ret < 0)
	{
		rte_exit(EXIT_FAILURE, "rte_eth_dev_start:err=%d, port=%u\n",
				 ret, PORT_ID);
	}
	rte_eth_promiscuous_enable(PORT_ID);

	std::cout << "port " << PORT_ID << " is initialized, ready to send" << std::endl;

	while (!force_quit)
	{
		payload_gen.gen_payload(batch);
		for (int i = 0; i < batch.size(); i++)
		{
			struct rte_mbuf *mbuf = batch[i];

			subflow_header header = sfheader_gen.gen_header();
			rte_memcpy((void *)rte_pktmbuf_prepend(mbuf, sizeof(subflow_header)),
					   (void *)&header,
					   sizeof(subflow_header));

			rte_memcpy((void *)rte_pktmbuf_prepend(mbuf, sizeof(struct rte_udp_hdr)),
					   (void *)&udp_hdrs[0],
					   sizeof(struct rte_udp_hdr));

			rte_memcpy((void *)rte_pktmbuf_prepend(mbuf, sizeof(struct rte_ipv4_hdr)),
					   (void *)&ip_hdrs[header.subflow_idx],
					   sizeof(struct rte_ipv4_hdr));

			rte_memcpy((void *)rte_pktmbuf_prepend(mbuf, sizeof(struct rte_ether_hdr)),
					   (void *)&eth_hdrs[0],
					   sizeof(struct rte_ether_hdr));
		}

		struct rte_mbuf **tx_pkts = batch.data();
		uint16_t nb_pkts = batch.size();

		uint16_t nb_tx = rte_eth_tx_burst(PORT_ID, 0, tx_pkts, nb_pkts);

		while (nb_tx < nb_pkts)
		{
			tx_pkts += nb_tx;
			nb_pkts -= nb_tx;

			nb_tx = rte_eth_tx_burst(PORT_ID, 0, tx_pkts, nb_pkts);
		}
	}

	std::cout << "Closing port ..." << PORT_ID << std::endl;
	;
	ret = rte_eth_dev_stop(PORT_ID);
	if (ret != 0)
	{
		std::cout << "rte_eth_dev_stop: err=" << ret << ", port=" << PORT_ID << std::endl;
	}
	rte_eth_dev_close(PORT_ID);
	std::cout << "Done" << std::endl;

	rte_mempool_free(mpool);
	rte_eal_cleanup();
}