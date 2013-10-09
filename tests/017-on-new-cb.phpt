--TEST--
Check that persistent id is null for non-persistent handles
--SKIPIF--
<?php require_once(dirname(__FILE__) . '/skipif.inc'); ?>
--FILE--
<?php
new MemcachedLite (null, function (MemcachedLite $obj, $persistent_id) {
							var_dump ($persistent_id);
						});

echo "ALL OK" . PHP_EOL;

?>
--EXPECT--
NULL
ALL OK