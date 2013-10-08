--TEST--
Test exists method
--SKIPIF--
<?php require_once(dirname(__FILE__) . '/skipif.inc'); ?>
--FILE--
<?php
$exists = null;

$memc = new MemcachedLite ();
$memc->add_server ('127.0.0.1', 11211);

$memc->set ('expires', 'hi', 1);
var_dump ($memc->exist ('expires'));
sleep (2);

var_dump ($memc->exist ('expires'));
echo "OK" . PHP_EOL;

?>
--EXPECT--
bool(true)
bool(false)
OK