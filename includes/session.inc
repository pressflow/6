<?php
// $Id: session.inc,v 1.44.2.6 2008/12/11 00:29:34 goba Exp $

/**
 * @file
 * User session handling functions.
 */

function sess_open($save_path, $session_name) {
  return TRUE;
}

function sess_close() {
  return TRUE;
}

function sess_read($key) {
  global $user;

  // Handle the case of first time visitors and clients that don't store cookies (eg. web crawlers).
  if (!isset($_COOKIE[session_name()])) {
    $user = drupal_anonymous_user();
    return '';
  }

  // Otherwise, if the session is still active, we have a record of the client's session in the database.
  $user = db_fetch_object(db_query("SELECT u.*, s.* FROM {users} u INNER JOIN {sessions} s ON u.uid = s.uid WHERE s.sid = '%s'", $key));

  // We found the client's session record and they are an authenticated user
  if ($user && $user->uid > 0) {
    // This is done to unserialize the data member of $user
    $user = drupal_unpack($user);

    // Add roles element to $user
    $user->roles = array();
    $user->roles[DRUPAL_AUTHENTICATED_RID] = 'authenticated user';
    $result = db_query("SELECT r.rid, r.name FROM {role} r INNER JOIN {users_roles} ur ON ur.rid = r.rid WHERE ur.uid = %d", $user->uid);
    while ($role = db_fetch_object($result)) {
      $user->roles[$role->rid] = $role->name;
    }
  }
  // We didn't find the client's record (session has expired), or they are an anonymous user.
  else {
    $session = isset($user->session) ? $user->session : '';
    $user = drupal_anonymous_user($session);
  }

  return $user->session;
}

function sess_write($key, $value) {
  global $user;

  // If saving of session data is disabled, or if a new empty anonymous session
  // has been started, do nothing. This keeps anonymous users, including
  // crawlers, out of the session table, unless they actually have something
  // stored in $_SESSION.
  if (!session_save_session() || ($user->uid == 0 && empty($_COOKIE[session_name()]) && empty($value))) {
    return TRUE;
  }

  db_query("UPDATE {sessions} SET uid = %d, cache = %d, hostname = '%s', session = '%s', timestamp = %d WHERE sid = '%s'", $user->uid, isset($user->cache) ? $user->cache : '', ip_address(), $value, time(), $key);
  if (db_affected_rows()) {
    // Last access time is updated no more frequently than once every 180 seconds.
    // This reduces contention in the users table.
    if ($user->uid && time() - $user->access > variable_get('session_write_interval', 180)) {
      db_query("UPDATE {users} SET access = %d WHERE uid = %d", time(), $user->uid);
    }
  }
  else {
    // If this query fails, another parallel request probably got here first.
    // In that case, any session data generated in this request is discarded.
    @db_query("INSERT INTO {sessions} (sid, uid, cache, hostname, session, timestamp) VALUES ('%s', %d, %d, '%s', '%s', %d)", $key, $user->uid, isset($user->cache) ? $user->cache : '', ip_address(), $value, time());
  }

  return TRUE;
}

/**
 * Propagate $_SESSION and set session cookie if not already set. This function
 * should be called before writing to $_SESSION, usually via
 * drupal_set_session().
 *
 * @param $start
 *   If FALSE, the session is not actually started. This is only used by
 *   drupal_session_is_started().
 * @return
 *   TRUE if session has already been started, or FALSE if it has not.
 */
function drupal_session_start($start = TRUE) {
  global $user;
  static $started = FALSE;
  if ($start && !$started) {
    $started = TRUE;

    // If the user had no session previously, save the user object and the
    // session data set previously in this request as they will be overriden
    // by sess_start().
    if (!isset($_COOKIE[session_name()])) {
      $saved_user = $user;
      $saved_session = (array) $_SESSION;
    }

    // Start the session.
    session_start();

    // Restore session data.
    if (!isset($_COOKIE[session_name()])) {
      $user = $saved_user;
      $_SESSION += $saved_session;
    }
  }
  return $started;
}

/**
 * Return whether a session has been started and the $_SESSION variable is
 * available.
 */
function drupal_session_is_started() {
  $result = drupal_session_start(FALSE);
  return $result;
}

/**
 * Write the session.
 */
function drupal_session_write() {
  if (!drupal_session_is_started() && !empty($_SESSION)) {
    drupal_session_start();
  }

  session_write_close();
}

/**
 * Get a session variable.
 *
 * @param $name
 *   The name of the variable to get. If not supplied, all variables are returned.
 * @return
 *   The value of the variable, or FALSE if the variable is not set.
 */
function drupal_get_session($name = NULL) {
  if (is_null($name)) {
    return $_SESSION;
  }
  elseif (isset($_SESSION[$name])) {
    return $_SESSION[$name];
  }
  else {
    return FALSE;
  }
}

/**
 * Set a session variable. The variable becomes accessible via $_SESSION[$name]
 * in the current and later requests. If there is no active PHP session prior
 * to the call, one is started automatically.
 *
 * Anonymous users generate less server load if their $_SESSION variable is
 * empty, so unused entries should be unset using unset($_SESSION['foo']).
 *
 * @param $name
 *   The name of the variable to set.
 * @param $value
 *   The value to set.
 */
function drupal_set_session($name, $value) {
  drupal_session_start();
  $_SESSION[$name] = $value;
}

/**
 * Called when an anonymous user becomes authenticated or vice-versa.
 */
function sess_regenerate() {
  $old_session_id = session_id();

  // We code around http://bugs.php.net/bug.php?id=32802 by destroying
  // the session cookie by setting expiration in the past (a negative
  // value).  This issue only arises in PHP versions before 4.4.0,
  // regardless of the Drupal configuration.
  // TODO: remove this when we require at least PHP 4.4.0
  if (isset($_COOKIE[session_name()])) {
    setcookie(session_name(), '', time() - 42000, '/');
  }

  session_regenerate_id();

  db_query("UPDATE {sessions} SET sid = '%s' WHERE sid = '%s'", session_id(), $old_session_id);
}

/**
 * Counts how many users have sessions. Can count either anonymous sessions or authenticated sessions.
 *
 * @param int $timestamp
 *   A Unix timestamp representing a point of time in the past.
 *   The default is 0, which counts all existing sessions.
 * @param boolean $anonymous
 *   TRUE counts only anonymous users.
 *   FALSE counts only authenticated users.
 * @return  int
 *   The number of users with sessions.
 */
function sess_count($timestamp = 0, $anonymous = true) {
  $query = $anonymous ? ' AND uid = 0' : ' AND uid > 0';
  return db_result(db_query('SELECT COUNT(sid) AS count FROM {sessions} WHERE timestamp >= %d'. $query, $timestamp));
}

/**
 * Called by PHP session handling with the PHP session ID to end a user's session.
 *
 * @param  string $sid
 *   the session id
 */
function sess_destroy_sid($sid) {
  db_query("DELETE FROM {sessions} WHERE sid = '%s'", $sid);
  // Unset cookie.
  extract(session_get_cookie_params());
  setcookie(session_name(), '', time() - 3600, $path, $domain, $secure, $httponly);
}

/**
 * End a specific user's session
 *
 * @param  string $uid
 *   the user id
 */
function sess_destroy_uid($uid) {
  db_query('DELETE FROM {sessions} WHERE uid = %d', $uid);
}

function sess_gc($lifetime) {
  // Be sure to adjust 'php_value session.gc_maxlifetime' to a large enough
  // value. For example, if you want user sessions to stay in your database
  // for three weeks before deleting them, you need to set gc_maxlifetime
  // to '1814400'. At that value, only after a user doesn't log in after
  // three weeks (1814400 seconds) will his/her session be removed.
  db_query("DELETE FROM {sessions} WHERE timestamp < %d", time() - $lifetime);

  return TRUE;
}

/**
 * Determine whether to save session data of the current request.
 *
 * This function allows the caller to temporarily disable writing of session data,
 * should the request end while performing potentially dangerous operations, such as
 * manipulating the global $user object.  See http://drupal.org/node/218104 for usage
 *
 * @param $status
 *   Disables writing of session data when FALSE, (re-)enables writing when TRUE.
 * @return
 *   FALSE if writing session data has been disabled. Otherwise, TRUE.
 */
function session_save_session($status = NULL) {
  static $save_session = TRUE;
  if (isset($status)) {
    $save_session = $status;
  }
  return ($save_session);
}
