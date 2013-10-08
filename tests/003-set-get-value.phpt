--TEST--
Set key and get different types
--SKIPIF--
<?php require_once(dirname(__FILE__) . '/skipif.inc'); ?>
--FILE--
<?php

require __DIR__ . '/test_driver.inc';

function run_setget_test ($memc) {
	
	$test = array (
				'str value',
				42,
				42.42,
				true,
				false,
				null,
				array (1,2,3),
				new stdClass ()
			);
	
	foreach ($test as $k => $value) {
		$memc->set ($k, $value, 10);
		$back = $memc->get ($k);

		if ((is_object ($value) && $back == $value) || $back === $value)
			continue;

		echo "Failure in " . gettype ($value) . PHP_EOL;
		var_dump ($value, $back);
		return;
	}
	echo "ALL OK" . PHP_EOL;
}

run_memc_lite_test (true, 'run_setget_test');
run_memc_lite_test (false, 'run_setget_test');

echo "OK" . PHP_EOL;

?>
--EXPECT--
BINARY PROTO
ALL OK
ASCII PROTO
ALL OK
OK