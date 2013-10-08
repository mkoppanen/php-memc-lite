--TEST--
Persistent connection callback invocation
--SKIPIF--
<?php require_once(dirname(__FILE__) . '/skipif.inc'); ?>
--FILE--
<?php
require __DIR__ . '/test_driver.inc';

$lite = new MemcachedLite ('my_conn', function (MemcachedLite $obj, $persistent_id) {
											$obj->set_binary_protocol (true);
											$obj->add_server (TEST_MEMCACHED_HOST);
											echo 'invoked once' . PHP_EOL;
                                          });

$lite = new MemcachedLite ('my_conn', function (MemcachedLite $obj, $persistent_id) {
											$obj->set_binary_protocol (true);
											$obj->add_server (TEST_MEMCACHED_HOST);
											echo 'invoked twice' . PHP_EOL;
                                          });

printf ("%d server" . PHP_EOL, count ($lite->get_servers ()));
echo "ALL OK" . PHP_EOL;

?>
--EXPECTF--
invoked once
1 server
ALL OK
    