<?php
// $Id: drupal_web_test_case.php,v 1.2.2.3.2.30 2009/02/13 02:08:02 boombatower Exp $
/**
 * @file
 * Provide required modifications to Drupal 7 core DrupalWebTestCase in order
 * for it to function properly in Drupal 6.
 *
 * Copyright 2008-2009 by Jimmy Berry ("boombatower", http://drupal.org/user/214218)
 */

require_once drupal_get_path('module', 'simpletest') . '/core/drupal_web_test_case.inc';

/**
 * Test case for typical Drupal tests.
 */
class DrupalWebTestCase extends DrupalWebTestCaseCore {

  /**
   * Internal helper: stores the assert.
   *
   * @param $status
   *   Can be 'pass', 'fail', 'exception'.
   *   TRUE is a synonym for 'pass', FALSE for 'fail'.
   * @param $message
   *   The message string.
   * @param $group
   *   Which group this assert belongs to.
   * @param $caller
   *   By default, the assert comes from a function whose name starts with
   *   'test'. Instead, you can specify where this assert originates from
   *   by passing in an associative array as $caller. Key 'file' is
   *   the name of the source file, 'line' is the line number and 'function'
   *   is the caller function itself.
   */
  protected function assert($status, $message = '', $group = 'Other', array $caller = NULL) {
    global $db_prefix;

    // Convert boolean status to string status.
    if (is_bool($status)) {
      $status = $status ? 'pass' : 'fail';
    }

    // Increment summary result counter.
    $this->results['#' . $status]++;

    // Get the function information about the call to the assertion method.
    if (!$caller) {
      $caller = $this->getAssertionCall();
    }

    // Switch to non-testing database to store results in.
    $current_db_prefix = $db_prefix;
    $db_prefix = $this->originalPrefix;

    // Creation assertion array that can be displayed while tests are running.
    $this->assertions[] = $assertion = array(
      'test_id' => $this->testId,
      'test_class' => get_class($this),
      'status' => $status,
      'message' => $message,
      'message_group' => $group,
      'function' => $caller['function'],
      'line' => $caller['line'],
      'file' => $caller['file'],
    );

    // Store assertion for display after the test has completed.
//    db_insert('simpletest')->fields($assertion)->execute();
    db_query("INSERT INTO {simpletest}
              (test_id, test_class, status, message, message_group, function, line, file)
              VALUES (%d, '%s', '%s', '%s', '%s', '%s', '%s', '%s')", array_values($assertion));

    // Return to testing prefix.
    $db_prefix = $current_db_prefix;
    return $status == 'pass' ? TRUE : FALSE;
  }

  /**
   * Creates a node based on default settings.
   *
   * @param $settings
   *   An associative array of settings to change from the defaults, keys are
   *   node properties, for example 'body' => 'Hello, world!'.
   * @return
   *   Created node object.
   */
  protected function drupalCreateNode($settings = array()) {
    // Populate defaults array
    $defaults = array(
      'body'      => $this->randomName(32),
      'title'     => $this->randomName(8),
      'comment'   => 2,
//      'changed'   => REQUEST_TIME,
      'changed'   => time(),
      'format'    => FILTER_FORMAT_DEFAULT,
      'moderate'  => 0,
      'promote'   => 0,
      'revision'  => 1,
      'log'       => '',
      'status'    => 1,
      'sticky'    => 0,
      'type'      => 'page',
      'revisions' => NULL,
      'taxonomy'  => NULL,
    );
    $defaults['teaser'] = $defaults['body'];
    // If we already have a node, we use the original node's created time, and this
    if (isset($defaults['created'])) {
      $defaults['date'] = format_date($defaults['created'], 'custom', 'Y-m-d H:i:s O');
    }
    if (empty($settings['uid'])) {
      global $user;
      $defaults['uid'] = $user->uid;
    }
    $node = ($settings + $defaults);
    $node = (object)$node;

    node_save($node);

    // small hack to link revisions to our test user
//    db_query('UPDATE {node_revision} SET uid = %d WHERE vid = %d', $node->uid, $node->vid);
    db_query('UPDATE {node_revisions} SET uid = %d WHERE vid = %d', $node->uid, $node->vid);
    return $node;
  }

  /**
   * Creates a custom content type based on default settings.
   *
   * @param $settings
   *   An array of settings to change from the defaults.
   *   Example: 'type' => 'foo'.
   * @return
   *   Created content type.
   */
  protected function drupalCreateContentType($settings = array()) {
    // find a non-existent random type name.
    do {
      $name = strtolower($this->randomName(3, 'type_'));
    } while (node_get_types('type', $name));

    // Populate defaults array
    $defaults = array(
      'type' => $name,
      'name' => $name,
      'description' => '',
      'help' => '',
      'min_word_count' => 0,
      'title_label' => 'Title',
      'body_label' => 'Body',
      'has_title' => 1,
      'has_body' => 1,
    );
    // imposed values for a custom type
    $forced = array(
      'orig_type' => '',
      'old_type' => '',
      'module' => 'node',
      'custom' => 1,
      'modified' => 1,
      'locked' => 0,
    );
    $type = $forced + $settings + $defaults;
    $type = (object)$type;

    $saved_type = node_type_save($type);
    node_types_rebuild();
    menu_rebuild(); // Drupal 6.

    $this->assertEqual($saved_type, SAVED_NEW, t('Created content type %type.', array('%type' => $type->type)));

    // Reset permissions so that permissions for this content type are available.
    $this->checkPermissions(array(), TRUE);

    return $type;
  }

  /**
   * Internal helper function; Create a role with specified permissions.
   *
   * @param $permissions
   *   Array of permission names to assign to role.
   * @return
   *   Role ID of newly created role, or FALSE if role creation failed.
   */
  protected function _drupalCreateRole(array $permissions = NULL) {
    // Generate string version of permissions list.
    if ($permissions === NULL) {
      $permissions = array('access comments', 'access content', 'post comments', 'post comments without approval');
    }

    if (!$this->checkPermissions($permissions)) {
      return FALSE;
    }

    // Create new role.
    $role_name = $this->randomName();
    db_query("INSERT INTO {role} (name) VALUES ('%s')", $role_name);
    $role = db_fetch_object(db_query("SELECT * FROM {role} WHERE name = '%s'", $role_name));
    $this->assertTrue($role, t('Created role of name: @role_name, id: @rid', array('@role_name' => $role_name, '@rid' => (isset($role->rid) ? $role->rid : t('-n/a-')))), t('Role'));
    if ($role && !empty($role->rid)) {
//      // Assign permissions to role and mark it for clean-up.
//      foreach ($permissions as $permission_string) {
//        db_query("INSERT INTO {role_permission} (rid, permission) VALUES (%d, '%s')", $role->rid, $permission_string);
//      }
//      $count = db_result(db_query("SELECT COUNT(*) FROM {role_permission} WHERE rid = %d", $role->rid));
//      $this->assertTrue($count == count($permissions), t('Created permissions: @perms', array('@perms' => implode(', ', $permissions))), t('Role'));
//      return $role->rid;

      // Assign permissions to role and mark it for clean-up.
      db_query("INSERT INTO {permission} (rid, perm) VALUES (%d, '%s')", $role->rid, implode(', ', $permissions));
      $perm = db_result(db_query("SELECT perm FROM {permission} WHERE rid = %d", $role->rid));
      $this->assertTrue(count(explode(', ', $perm)) == count($permissions), t('Created permissions: @perms', array('@perms' => implode(', ', $permissions))), t('Role'));
      return $role->rid;
    }
    else {
      return FALSE;
    }
  }

/**
   * Check to make sure that the array of permissions are valid.
   *
   * @param $permissions
   *   Permissions to check.
   * @param $reset
   *   Reset cached available permissions.
   * @return
   *   TRUE or FALSE depending on whether the permissions are valid.
   */
  protected function checkPermissions(array $permissions, $reset = FALSE) {
    static $available;

    if (!isset($available) || $reset) {
//      $available = array_keys(module_invoke_all('perm'));
      $available = module_invoke_all('perm');
    }

    $valid = TRUE;
    foreach ($permissions as $permission) {
      if (!in_array($permission, $available)) {
        $this->fail(t('Invalid permission %permission.', array('%permission' => $permission)), t('Role'));
        $valid = FALSE;
      }
    }
    return $valid;
  }

  /*
   * Logs a user out of the internal browser, then check the login page to confirm logout.
   */
  protected function drupalLogout() {
    // Make a request to the logout page.
//    $this->drupalGet('user/logout');
    $this->drupalGet('logout');

    // Load the user page, the idea being if you were properly logged out you should be seeing a login screen.
    $this->drupalGet('user');
    $pass = $this->assertField('name', t('Username field found.'), t('Logout'));
    $pass = $pass && $this->assertField('pass', t('Password field found.'), t('Logout'));

    $this->isLoggedIn = !$pass;
  }

  /**
   * Generates a random database prefix, runs the install scripts on the
   * prefixed database and enable the specified modules. After installation
   * many caches are flushed and the internal browser is setup so that the
   * page requests will run on the new prefix. A temporary files directory
   * is created with the same name as the database prefix.
   *
   * @param ...
   *   List of modules to enable for the duration of the test.
   */
  protected function setUp() {
    global $db_prefix, $user;

    // Store necessary current values before switching to prefixed database.
    $this->originalPrefix = $db_prefix;
    $clean_url_original = variable_get('clean_url', 0);

    // Generate temporary prefixed database to ensure that tests have a clean starting point.
//    $db_prefix = Database::getConnection()->prefixTables('{simpletest' . mt_rand(1000, 1000000) . '}');
    $db_prefix = 'simpletest' . mt_rand(1000, 1000000);

//    include_once DRUPAL_ROOT . '/includes/install.inc';
    include_once './includes/install.inc';
    drupal_install_system();

//    $this->preloadRegistry();

    // Add the specified modules to the list of modules in the default profile.
    $args = func_get_args();
//    $modules = array_unique(array_merge(drupal_get_profile_modules('default', 'en'), $args));
    $modules = array_unique(array_merge(drupal_verify_profile('default', 'en'), $args));
//    drupal_install_modules($modules, TRUE);
    drupal_install_modules($modules);

    // Because the schema is static cached, we need to flush
    // it between each run. If we don't, then it will contain
    // stale data for the previous run's database prefix and all
    // calls to it will fail.
    drupal_get_schema(NULL, TRUE);

    // Run default profile tasks.
    $task = 'profile';
    default_profile_tasks($task, '');

    // Rebuild caches.
    actions_synchronize();
    _drupal_flush_css_js();
    $this->refreshVariables();
    $this->checkPermissions(array(), TRUE);

    // Log in with a clean $user.
    $this->originalUser = $user;
//    drupal_save_session(FALSE);
    session_save_session(FALSE);
    $user = user_load(array('uid' => 1));

    // Restore necessary variables.
    variable_set('install_profile', 'default');
    variable_set('install_task', 'profile-finished');
    variable_set('clean_url', $clean_url_original);
    variable_set('site_mail', 'simpletest@example.com');

    // Use temporary files directory with the same prefix as database.
    $this->originalFileDirectory = file_directory_path();
    variable_set('file_directory_path', file_directory_path() . '/' . $db_prefix);
    $directory = file_directory_path();
    file_check_directory($directory, FILE_CREATE_DIRECTORY); // Create the files directory.
    set_time_limit($this->timeLimit);
  }

  /**
   * Delete created files and temporary files directory, delete the tables created by setUp(),
   * and reset the database prefix.
   */
  protected function tearDown() {
    global $db_prefix, $user;
    if (preg_match('/simpletest\d+/', $db_prefix)) {
      // Delete temporary files directory and reset files directory path.
      simpletest_clean_temporary_directory(file_directory_path());
      variable_set('file_directory_path', $this->originalFileDirectory);

      // Remove all prefixed tables (all the tables in the schema).
      $schema = drupal_get_schema(NULL, TRUE);
      $ret = array();
      foreach ($schema as $name => $table) {
        db_drop_table($ret, $name);
      }

      // Return the database prefix to the original.
      $db_prefix = $this->originalPrefix;

      // Return the user to the original one.
      $user = $this->originalUser;
//      drupal_save_session(TRUE);
      session_save_session(TRUE);

      // Ensure that internal logged in variable and cURL options are reset.
      $this->isLoggedIn = FALSE;
      $this->additionalCurlOptions = array();

      // Reload module list and implementations to ensure that test module hooks
      // aren't called after tests.
      module_list(TRUE);
      module_implements('', '', TRUE);

      // Reset the Field API.
//      field_cache_clear();

      // Rebuild caches.
      $this->refreshVariables();

      // Close the CURL handler.
      $this->curlClose();
    }
  }
}
