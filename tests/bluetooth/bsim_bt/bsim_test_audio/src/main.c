/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "bstests.h"

extern struct bst_test_list *test_tbs_install(
	struct bst_test_list *tests);
extern struct bst_test_list *test_ccp_install(
	struct bst_test_list *tests);

bst_test_install_t test_installers[] = {
	test_tbs_install,
	test_ccp_install,
	NULL
};

void main(void)
{
	bst_main();
}
