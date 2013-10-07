--TEST--
Set key and get different types
--SKIPIF--
<?php require_once(dirname(__FILE__) . '/skipif.inc'); ?>
--FILE--
<?php
$memc = new MemcachedLite ();
$memc->add_server ('localhost', 11211);

try {
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
		$memc->set ($k, $value);
		$back = $memc->get ($k);

		if ((is_object ($value) && $back == $value) || $back === $value)
			continue;

		echo "Failure in " . gettype ($value) . PHP_EOL;
		var_dump ($value, $back);
	}
} catch (Exception $e) {
	echo "fail: " . $e->getMessage () . PHP_EOL;
}

echo "OK" . PHP_EOL;

?>
--EXPECT--
OK