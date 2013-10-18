--TEST--
Test increment and decrement
--SKIPIF--
<?php require_once(dirname(__FILE__) . '/skipif.inc'); ?>
--FILE--
<?php

require __DIR__ . '/test_driver.inc';

$memc = new MemcachedLite ();
$memc->set_binary_protocol (true);
var_dump ($memc->get_binary_protocol ());

$memc->add_server (TEST_MEMCACHED_HOST, TEST_MEMCACHED_PORT);

$memc->set ('interval_value', 41, 10);
$new_value = $memc->increment ('interval_value', 1);
var_dump ($new_value);

$new_value = $memc->decrement ('interval_value', 42);
var_dump ($new_value);

$new_value = $memc->increment ('interval_value', 7);
var_dump ($new_value);

// Should overflow to string
$new_value = $memc->increment ('test_key', 1000, 10, PHP_INT_MAX);
var_dump ($new_value);

echo "OK" . PHP_EOL;

?>
--EXPECTF--
bool(true)
int(42)
int(0)
int(7)
string(%d) "%d"
OK
