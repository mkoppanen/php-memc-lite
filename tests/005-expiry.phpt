--TEST--
Expiring records
--SKIPIF--
<?php require_once(dirname(__FILE__) . '/skipif.inc'); ?>
--FILE--
<?php

require __DIR__ . '/test_driver.inc';

function run_expiry_test ($memc)
{
	$memc->set ('expires', 'hi', 1);
	sleep (2);
	var_dump ($memc->get ('expires'));
}

run_memc_lite_test (true, 'run_expiry_test');
run_memc_lite_test (false, 'run_expiry_test');

echo "OK" . PHP_EOL;

?>
--EXPECT--
BINARY PROTO
NULL
ASCII PROTO
NULL
OK