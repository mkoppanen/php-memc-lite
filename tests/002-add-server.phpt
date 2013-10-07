--TEST--
Add server(s)
--SKIPIF--
<?php require_once(dirname(__FILE__) . '/skipif.inc'); ?>
--FILE--
<?php
$memc = new MemcachedLite ();

try {
	$memc->add_server ('localhost', 11211);
	$memc->add_server ('127.0.0.1', 11211);
} catch (Exception $e) {
	echo "fail";
}

echo "OK" . PHP_EOL;

?>
--EXPECT--
OK