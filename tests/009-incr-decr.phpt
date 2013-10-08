--TEST--
Test increment and decrement
--SKIPIF--
<?php require_once(dirname(__FILE__) . '/skipif.inc'); ?>
--FILE--
<?php
$memc = new MemcachedLite ();
$memc->set_binary_protocol (true);
var_dump ($memc->get_binary_protocol ());

$memc->add_server ('127.0.0.1', 11211);

$memc->set ('interval_value', 41, 10);
$new_value = $memc->increment ('interval_value', 1);
var_dump ($new_value);

$new_value = $memc->decrement ('interval_value', 42);
var_dump ($new_value);

$new_value = $memc->increment ('interval_value', 7);
var_dump ($new_value);

echo "OK" . PHP_EOL;

?>
--EXPECT--
bool(true)
int(42)
int(0)
int(7)
OK
