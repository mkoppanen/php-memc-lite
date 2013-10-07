--TEST--
Init the object and destroy
--SKIPIF--
<?php require_once(dirname(__FILE__) . '/skipif.inc'); ?>
--FILE--
<?php
$memc = new MemcachedLite ();
unset ($memc);

echo "OK" . PHP_EOL;

?>
--EXPECTF--
OK