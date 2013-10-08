--TEST--
Test adding values
--SKIPIF--
<?php require_once(dirname(__FILE__) . '/skipif.inc'); ?>
--FILE--
<?php

require __DIR__ . '/test_driver.inc';

function run_add_test ($memc)
{
	$key = uniqid ('memc_lite_add_');

	$memc->set ($key, 'initial value', 10);
	$memc->add ($key, 'new value', 10);

	echo $memc->get ($key) . PHP_EOL;
}

run_memc_lite_test (true, 'run_add_test');
run_memc_lite_test (false, 'run_add_test');

echo "OK" . PHP_EOL;

?>
--EXPECT--
BINARY PROTO
initial value
ASCII PROTO
initial value
OK