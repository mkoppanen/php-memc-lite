--TEST--
CAS tokens
--SKIPIF--
<?php require_once(dirname(__FILE__) . '/skipif.inc'); ?>
--FILE--
<?php

require __DIR__ . '/test_driver.inc';

function run_cas_test ($memc)
{
	$key = 'hello_world';
	
	$exists = null;
	$cas = -1;	

	// Set initial value to 'hi' with expiration of ten
	$memc->set ($key, 'hi', 10);

	// We should be able to get cas token back
	$value = $memc->get ($key, $exists, $cas);
	printf ("key=[%s] value=[%s] exists=[%d] cas=[%d]" . PHP_EOL, $key, $value, $exists, is_string ($cas));
	$old_cas = $cas;
	
	// This should not throw exception, correct cas token
	$memc->set ($key, 'hi again', 10, $cas);

	// Let's see that the value is correct
	$value = $memc->get ($key, $exists, $cas);
	printf ("key=[%s] value=[%s] exists=[%d] cas=[%d]" . PHP_EOL, $key, $value, $exists, is_string ($cas));
	
	try {
		// Setting this key should fail because of wrong cas
		$memc->set ($key, 'hohoho', 10, $old_cas);
	} catch (Exception $e) {
		echo "CAS failed as expected" . PHP_EOL;
	}

	// Test cas value with non-existent key
	$key = 'this_key_should_not_exist_111';
	$memc->get ($key, $exists, $cas);
	printf ("key=[%s] value=[%s] exists=[%d] cas=[%d]" . PHP_EOL, $key, $value, $exists, is_string ($cas));
}


run_memc_lite_test (true, 'run_cas_test');
run_memc_lite_test (false, 'run_cas_test');


echo "OK" . PHP_EOL;

?>
--EXPECT--
BINARY PROTO
key=[hello_world] value=[hi] exists=[1] cas=[1]
key=[hello_world] value=[hi again] exists=[1] cas=[1]
key=[this_key_should_not_exist_111] value=[hi again] exists=[0] cas=[0]
ASCII PROTO
key=[hello_world] value=[hi] exists=[1] cas=[1]
key=[hello_world] value=[hi again] exists=[1] cas=[1]
key=[this_key_should_not_exist_111] value=[hi again] exists=[0] cas=[0]
OK