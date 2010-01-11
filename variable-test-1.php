<?php
include_once './includes/bootstrap.inc';
drupal_bootstrap(DRUPAL_BOOTSTRAP_FULL);

variable_set('testing_123', time());

echo variable_get('testing_123', 'default stuff') . "\n";
