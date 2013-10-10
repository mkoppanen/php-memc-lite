--TEST--
Test touch
--SKIPIF--
<?php require_once(dirname(__FILE__) . '/skipif.inc'); ?>
--FILE--
<?php

require __DIR__ . '/test_driver.inc';

function run_expiry_test ($memc)
{
	$memc->set ('expires', 'hi', 1);
	$memc->touch ('expires', 10);
	sleep (2);
	var_dump ($memc->get ('expires'));
	
	var_dump ($memc->touch (uniqid ('this_does_not_exist_')));
}

run_memc_lite_test (true, 'run_expiry_test');
run_memc_lite_test (false, 'run_expiry_test');

echo "OK" . PHP_EOL;

?>
--EXPECT--
BINARY PROTO
string(2) "hi"
bool(false)
ASCII PROTO
string(2) "hi"
bool(false)
OK