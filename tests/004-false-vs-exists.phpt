--TEST--
Difference between null and not exists
--SKIPIF--
<?php require_once(dirname(__FILE__) . '/skipif.inc'); ?>
--FILE--
<?php
$exists = null;

$memc = new MemcachedLite ();
$memc->add_server ('127.0.0.1', 11211);

$memc->set ('this_is_null', null);
$memc->get ('this_is_null', $exists);
var_dump ($exists);

$memc->get ('this_should_fail_' . uniqid (), $exists);
var_dump ($exists);

echo "OK" . PHP_EOL;

?>
--EXPECT--
bool(true)
bool(false)
OK