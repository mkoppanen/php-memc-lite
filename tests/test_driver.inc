<?php

function run_memc_lite_test ($binary, $callable)
{
	echo ($binary ? "BINARY PROTO" : "ASCII PROTO") . PHP_EOL;
	
	$memc = new MemcachedLite ();
	$memc->set_binary_protocol ($binary);
	$memc->add_server ('127.0.0.1', 11211);

	$callable ($memc);
}