--TEST--
Test exists method
--SKIPIF--
<?php require_once(dirname(__FILE__) . '/skipif.inc'); ?>
--FILE--
<?php
require __DIR__ . '/test_driver.inc';

$exists = null;

$memc = new MemcachedLite ();
$memc->add_server (TEST_MEMCACHED_HOST, TEST_MEMCACHED_PORT);

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