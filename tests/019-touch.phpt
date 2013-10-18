--TEST--
Test touch
--SKIPIF--
<?php require_once(dirname(__FILE__) . '/skipif.inc'); ?>
--FILE--
<?php

require __DIR__ . '/test_driver.inc';

function run_touch_test ($memc)
{
	$key = uniqid ('touch_key_');
	
	$memc->set ($key, 'hi', 5);
	$memc->touch ($key, 10);
	sleep (2);

	$exists = null;
	var_dump ($memc->get ($key, $exists));
	var_dump ($exists);
	
	var_dump ($memc->touch (uniqid ('this_does_not_exist_')));
}

run_memc_lite_test (true, 'run_touch_test');
run_memc_lite_test (false, 'run_touch_test');

echo "OK" . PHP_EOL;

?>
--EXPECT--
BINARY PROTO
string(2) "hi"
bool(true)
bool(false)
ASCII PROTO
string(2) "hi"
bool(true)
bool(false)
OK