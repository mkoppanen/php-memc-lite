--TEST--
Test distribution method
--SKIPIF--
<?php require_once(dirname(__FILE__) . '/skipif.inc'); ?>
--FILE--
<?php

$memc = new MemcachedLite ();
$memc->set_distribution (MemcachedLite::DISTRIBUTION_MODULO);
var_dump ($memc->get_distribution () == MemcachedLite::DISTRIBUTION_MODULO);

$memc->set_distribution (MemcachedLite::DISTRIBUTION_KETAMA);
var_dump ($memc->get_distribution () == MemcachedLite::DISTRIBUTION_KETAMA);

$memc->set_distribution (MemcachedLite::DISTRIBUTION_VBUCKET);
var_dump ($memc->get_distribution () == MemcachedLite::DISTRIBUTION_VBUCKET);

echo "OK" . PHP_EOL;

?>
--EXPECT--
bool(true)
bool(true)
bool(true)
OK