/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2014 Intel Corporation
 */

// #include <stdio.h>
// #include <string.h>
// #include <stdint.h>
// #include <errno.h>
// #include <sys/queue.h>

// #include <rte_memory.h>
// #include <rte_launch.h>
// #include <rte_eal.h>
// #include <rte_per_lcore.h>
// #include <rte_lcore.h>
// #include <rte_debug.h>

// static int
// lcore_hello(__rte_unused void *arg)
// {
// 	unsigned lcore_id;
// 	lcore_id = rte_lcore_id();
// 	printf("hello from core %u\n", lcore_id);
// 	return 0;
// }

// int
// main(int argc, char **argv)
// {
// 	int ret;
// 	unsigned lcore_id;

// 	ret = rte_eal_init(argc, argv);
// 	if (ret < 0)
// 		rte_panic("Cannot init EAL\n");

// 	/* call lcore_hello() on every worker lcore */
// 	RTE_LCORE_FOREACH_WORKER(lcore_id) {
// 		rte_eal_remote_launch(lcore_hello, NULL, lcore_id);
// 	}

// 	/* call it on main lcore too */
// 	lcore_hello(NULL);

// 	rte_eal_mp_wait_lcore();

// 	/* clean up the EAL */
// 	rte_eal_cleanup();

// 	return 0;
// }

#include <iostream>

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

int main()
{
	std::cout << "fuck, I need sometime to get used to c++, I'm not quite confident about my current rust implementation" << std::endl;

	subflow_header_generator header_gen = subflow_header_generator();
	for(int i=0; i<100; i++) {
		subflow_header header = header_gen.gen_header();
		std::cout<<"subflow_idx: "<<header.subflow_idx<<", seq_num: "<<header.seq_num<<std::endl;
	}
}