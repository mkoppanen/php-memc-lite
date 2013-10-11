<?php

//define ('MEMC_LITE_SASL_USER', 'mikko');
//define ('MEMC_LITE_SASL_PASS', 'mikkomikko');

define ('TEST_MEMCACHED_HOST', '127.0.0.1');
define ('TEST_MEMCACHED_PORT', 11211);

define ('TEST_MEMCACHED_SASL_HOST', '127.0.0.1');
define ('TEST_MEMCACHED_SASL_PORT', 11212);

function run_memc_lite_test ($binary, $callable, $sasl_server = false)
{
	echo ($binary ? "BINARY PROTO" : "ASCII PROTO") . PHP_EOL;

	$memc = new MemcachedLite ();
	$memc->set_binary_protocol ($binary);

	if ($sasl_server)
	     $memc->add_server (TEST_MEMCACHED_SASL_HOST, TEST_MEMCACHED_SASL_PORT);
	else
	    $memc->add_server (TEST_MEMCACHED_HOST, TEST_MEMCACHED_PORT);

	$callable ($memc);
}