--TEST--
Test overflowing in get
--SKIPIF--
<?php require_once(dirname(__FILE__) . '/skipif.inc'); ?>
--FILE--
<?php

require __DIR__ . '/test_driver.inc';

$memc = new MemcachedLite ();
$memc->set_binary_protocol (true);
$memc->add_server (TEST_MEMCACHED_HOST, TEST_MEMCACHED_PORT);

$memc->set ('my_value', PHP_INT_MAX, 10);
$new_value = $memc->increment ('my_value', 5000);

var_dump ($memc->get ('my_value'));
echo "OK" . PHP_EOL;

?>
--EXPECTF--
string(%d) "%d"
OK
