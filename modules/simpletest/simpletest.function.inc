<?php
// $Id: simpletest.function.inc,v 1.1.2.2 2009/04/23 05:39:51 boombatower Exp $
// Core: Id: common.inc,v 1.878 2009/04/22 09:45:02 dries Exp
/**
 * @file
 * Provide backported function from Drupal 7 core required by SimpleTest.
 *
 * Copyright 2008-2009 by Jimmy Berry ("boombatower", http://drupal.org/user/214218)
 */

/*
 * From common.inc.
 */

// Set the Drupal custom error handler.
set_error_handler('_drupal_error_handler');
set_exception_handler('_drupal_exception_handler');

/**
 * Custom PHP error handler.
 *
 * @param $error_level
 *   The level of the error raised.
 * @param $message
 *   The error message.
 * @param $filename
 *   The filename that the error was raised in.
 * @param $line
 *   The line number the error was raised at.
 * @param $context
 *   An array that points to the active symbol table at the point the error occurred.
 */
function _drupal_error_handler($error_level, $message, $filename, $line, $context) {
  if ($error_level & error_reporting()) {
    // All these constants are documented at http://php.net/manual/en/errorfunc.constants.php
    $types = array(
      E_ERROR => 'Error',
      E_WARNING => 'Warning',
      E_PARSE => 'Parse error',
      E_NOTICE => 'Notice',
      E_CORE_ERROR => 'Core error',
      E_CORE_WARNING => 'Core warning',
      E_COMPILE_ERROR => 'Compile error',
      E_COMPILE_WARNING => 'Compile warning',
      E_USER_ERROR => 'User error',
      E_USER_WARNING => 'User warning',
      E_USER_NOTICE => 'User notice',
      E_STRICT => 'Strict warning',
      E_RECOVERABLE_ERROR => 'Recoverable fatal error'
    );
    $backtrace = debug_backtrace();

    $caller = _drupal_get_last_caller(debug_backtrace());

    // We treat recoverable errors as fatal.
    _drupal_log_error(array(
      '%type' => isset($types[$error_level]) ? $types[$error_level] : 'Unknown error',
      '%message' => $message,
      '%function' => $caller['function'],
      '%file' => $caller['file'],
      '%line' => $caller['line'],
    ), $error_level == E_RECOVERABLE_ERROR);
  }
}

/**
 * Custom PHP exception handler.
 *
 * Uncaught exceptions are those not enclosed in a try/catch block. They are
 * always fatal: the execution of the script will stop as soon as the exception
 * handler exits.
 *
 * @param $exception
 *   The exception object that was thrown.
 */
function _drupal_exception_handler($exception) {
  // Log the message to the watchdog and return an error page to the user.
  _drupal_log_error(_drupal_decode_exception($exception), TRUE);
}

/**
 * Decode an exception, especially to retrive the correct caller.
 *
 * @param $exception
 *   The exception object that was thrown.
 * @return An error in the format expected by _drupal_log_error().
 */
function _drupal_decode_exception($exception) {
  $backtrace = $exception->getTrace();
  // Add the line throwing the exception to the backtrace.
  array_unshift($backtrace, array('line' => $exception->getLine(), 'file' => $exception->getFile()));

  // For PDOException errors, we try to return the initial caller,
  // skipping internal functions of the database layer.
  if ($exception instanceof PDOException) {
    // The first element in the stack is the call, the second element gives us the caller.
    // We skip calls that occurred in one of the classes of the database layer
    // or in one of its global functions.
//    $db_functions = array('db_query', 'pager_query', 'db_query_range', 'db_query_temporary', 'update_sql');
    $db_functions = array('db_query', '_db_query', 'pager_query', 'db_query_range', 'db_query_temporary', 'update_sql');
    while (!empty($backtrace[1]) && ($caller = $backtrace[1]) &&
         ((isset($caller['class']) && (strpos($caller['class'], 'Query') !== FALSE || strpos($caller['class'], 'Database') !== FALSE)) ||
         in_array($caller['function'], $db_functions))) {
      // We remove that call.
      array_shift($backtrace);
    }
  }
  $caller = _drupal_get_last_caller($backtrace);

  return array(
    '%type' => get_class($exception),
    '%message' => $exception->getMessage(),
    '%function' => $caller['function'],
    '%file' => $caller['file'],
    '%line' => $caller['line'],
  );
}

/**
 * Log a PHP error or exception, display an error page in fatal cases.
 *
 * @param $error
 *   An array with the following keys: %type, %message, %function, %file, %line.
 * @param $fatal
 *   TRUE if the error is fatal.
 */
function _drupal_log_error($error, $fatal = FALSE) {
  // Initialize a maintenance theme early if the boostrap was not complete.
  // Do it early because drupal_set_message() triggers an init_theme().
  if ($fatal && (drupal_get_bootstrap_phase() != DRUPAL_BOOTSTRAP_FULL)) {
    unset($GLOBALS['theme']);
    if (!defined('MAINTENANCE_MODE')) {
      define('MAINTENANCE_MODE', 'error');
    }
    drupal_maintenance_theme();
  }

  // When running inside the testing framework, we relay the errors
  // to the tested site by the way of HTTP headers.
  if (preg_match("/^simpletest\d+/", $_SERVER['HTTP_USER_AGENT']) && !headers_sent() && (!defined('SIMPLETEST_COLLECT_ERRORS') || SIMPLETEST_COLLECT_ERRORS)) {
    static $number = 0;
    $assertion = array(
      $error['%message'],
      $error['%type'],
      array(
        'function' => $error['%function'],
        'file' => $error['%file'],
        'line' => $error['%line'],
      ),
    );
    header('X-Drupal-Assertion-' . $number . ': ' . rawurlencode(serialize($assertion)));
    $number++;
  }

  // Force display of error messages in update.php or if the proper error
  // reporting level is set.
  $error_level = variable_get('error_level', 2);
  if ($error_level == 2 || ($error_level == 1 && $error['%type'] != 'Notice') || (defined('MAINTENANCE_MODE') && MAINTENANCE_MODE == 'update')) {
    drupal_set_message(t('%type: %message in %function (line %line of %file).', $error), 'error');
  }

  try {
    watchdog('php', '%type: %message in %function (line %line of %file).', $error, WATCHDOG_ERROR);
  }
  catch (Exception $e) {
    $new_error = _drupal_decode_exception($e);
    drupal_set_message(t('%type: %message in %function (line %line of %file).', $new_error), 'error');
  }

  if ($fatal) {
    drupal_set_header('503 Service unavailable');
    drupal_set_title(t('Error'));
    if (!defined('MAINTENANCE_MODE') && drupal_get_bootstrap_phase() == DRUPAL_BOOTSTRAP_FULL) {
      // To conserve CPU and bandwidth, omit the blocks.
      $page = drupal_get_page(t('The website encountered an unexpected error. Please try again later.'));
      $page['#show_blocks'] = FALSE;
      print drupal_render_page($page);
    }
    else {
      print theme('maintenance_page', t('The website encountered an unexpected error. Please try again later.'), FALSE);
    }
    exit;
  }
}

/**
 * Gets the last caller from a backtrace.
 *
 * @param $backtrace
 *   A standard PHP backtrace.
 * @return
 *   An associative array with keys 'file', 'line' and 'function'.
 */
function _drupal_get_last_caller($backtrace) {
  // Errors that occur inside PHP internal functions
  // do not generate information about file and line.
  while ($backtrace && !isset($backtrace[0]['line'])) {
    array_shift($backtrace);
  }

  // The first trace is the call itself.
  // It gives us the line and the file of the last call.
  $call = $backtrace[0];

  // The second call give us the function where the call originated.
  if (isset($backtrace[1])) {
    if (isset($backtrace[1]['class'])) {
      $call['function'] = $backtrace[1]['class'] . $backtrace[1]['type'] . $backtrace[1]['function'] . '()';
    }
    else {
      $call['function'] = $backtrace[1]['function'] . '()';
    }
  }
  else {
    $call['function'] = 'main()';
  }
  return $call;
}
