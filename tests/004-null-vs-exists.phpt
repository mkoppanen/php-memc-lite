--TEST--
Difference between null and not exists
--SKIPIF--
<?php require_once(dirname(__FILE__) . '/skipif.inc'); ?>
--FILE--
<?php

require __DIR__ . '/test_driver.inc';

function run_null_test ($memc)
{
	$exists = null;

	$memc->set ('this_is_null', null, 10);
	$v = $memc->get ('this_is_null', $exists);
	var_dump ($v, $exists);

	$v = $memc->get ('this_should_fail_' . uniqid (), $exists);
	var_dump ($v, $exists);
}

run_memc_lite_test (true, 'run_null_test');
run_memc_lite_test (false, 'run_null_test');

echo "OK" . PHP_EOL;

?>
--EXPECT--
BINARY PROTO
NULL
bool(true)
NULL
bool(false)
ASCII PROTO
NULL
bool(true)
NULL
bool(false)
OK