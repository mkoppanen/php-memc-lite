<?php

define ('TEST_MEMCACHED_HOST', '127.0.0.1');
define ('TEST_MEMCACHED_PORT', 11211);

function run_memc_lite_test ($binary, $callable)
{
	echo ($binary ? "BINARY PROTO" : "ASCII PROTO") . PHP_EOL;
	
	$memc = new MemcachedLite ();
	$memc->set_binary_protocol ($binary);
	$memc->add_server (TEST_MEMCACHED_HOST, TEST_MEMCACHED_PORT);

	$callable ($memc);
}