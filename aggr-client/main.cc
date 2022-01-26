#include <iostream>
#include <vector>

#include <signal.h>

#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>

// define some macros as configuration parameters
#define BATCH_SIZE 32
#define NB_MBUF 8192
#define MEMPOOL_CACHE_SIZE 256

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
	uint64_t subflow_idx;
	uint64_t seq_num;
};

class subflow_header_generator
{
public:
	subflow_header_generator() : _counter(0) {}

	subflow_header gen_header()
	{
		uint64_t curr_idx = _counter % 5;
		subflow_header header;
		if (curr_idx < 3)
		{
			header.subflow_idx = curr_idx;
			header.seq_num = _counter;
		}
		else
		{
			header.subflow_idx = curr_idx - 3;
			header.seq_num = _counter;
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
		assert(batch.size() == BATCH_SIZE);

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

	// prepare the payload generator
	std::vector<uint8_t> payload_template(1450, 0xfe);
	payload_generator payload_gen = payload_generator(mpool, std::move(payload_template));

	// prepare the subflow header generator
	subflow_header_generator sfheader_gen = subflow_header_generator();

	// prepare the udp headers
	std::vector<struct rte_udp_hdr> udp_hdrs;
	struct rte_udp_hdr udp_hdr;
	udp_hdr.src_port = rte_cpu_to_be_16(1024);
	udp_hdr.dst_port = rte_cpu_to_be_16(250);
	udp_hdr.dgram_len = 0;
	udp_hdr.dgram_cksum = 0;
	udp_hdrs.push_back(udp_hdr);

	// prepare the ethernet headers
	uint8_t smac[6] = {0x22, 0x11, 0x33, 0x44, 0x55, 0x66};
	uint8_t dmac[6] = {0x11, 0x44, 0x55, 0x66, 0x77, 0x88};
	std::vector<struct rte_ether_hdr> eth_hdrs;
	struct rte_ether_hdr eth_hdr;
	rte_ether_addr_copy((rte_ether_addr*)smac, &eth_hdr.s_addr);
	rte_ether_addr_copy((rte_ether_addr*)dmac, &eth_hdr.d_addr);
	eth_hdr.ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);
	
	// payload_gen.gen_payload(batch);
	// for (int i = 0; i < batch.size(); i++)
	// {
	// 	struct rte_mbuf *mbuf = batch[i];
	// 	std::cout << i << "th packet: len=" << mbuf->data_len << ", header_offset=" << mbuf->data_off << std::endl;

	// 	for (int j = 0; j < 1450; j++)
	// 	{
	// 		uint8_t val = *rte_pktmbuf_mtod_offset(mbuf, uint8_t *, j);
	// 		assert(val != 0xfe);
	// 	}

	// 	rte_pktmbuf_free(mbuf);
	// }

	while (!force_quit)
	{
	}

	rte_mempool_free(mpool);

	// subflow_header_generator header_gen = subflow_header_generator();
	// for (int i = 0; i < 100; i++)
	// {
	// 	subflow_header header = header_gen.gen_header();
	// 	std::cout << "subflow_idx: " << header.subflow_idx << ", seq_num: " << header.seq_num << std::endl;
	// }
}