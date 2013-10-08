--TEST--
Test delete method
--SKIPIF--
<?php require_once(dirname(__FILE__) . '/skipif.inc'); ?>
--FILE--
<?php

require __DIR__ . '/test_driver.inc';

function run_delete_test ($memc)
{
	$memc->set ('my_key', 'hi', 10);
	var_dump ($memc->exist ('my_key'));

	$memc->delete ('my_key');
	var_dump ($memc->exist ('my_key'));
}

run_memc_lite_test (true, 'run_delete_test');
run_memc_lite_test (false, 'run_delete_test');

echo "OK" . PHP_EOL;

?>
--EXPECT--
BINARY PROTO
bool(true)
bool(false)
ASCII PROTO
bool(true)
bool(false)
OK