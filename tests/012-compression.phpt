--TEST--
Test compression
--SKIPIF--
<?php require_once(dirname(__FILE__) . '/skipif.inc'); ?>
--FILE--
<?php
require __DIR__ . '/test_driver.inc';

function round_trip ($memc, $value)
{
	$key = uniqid ('roundtrip_');
	
	$memc->set ($key, $value, 10);
	if ($memc->get ($key) !== $value)
		echo "Fail" . PHP_EOL;
	else
		echo "OK" . PHP_EOL;
}

function run_compression_tests ($memc)
{
	round_trip ($memc, str_repeat ('abcde12345', 100));
	round_trip ($memc, array (str_repeat ('abcde12345', 100)));
}

run_memc_lite_test (true, 'run_compression_tests');
run_memc_lite_test (false, 'run_compression_tests');

echo "ALL OK" . PHP_EOL;

?>
--EXPECT--
BINARY PROTO
OK
OK
ASCII PROTO
OK
OK
ALL OK