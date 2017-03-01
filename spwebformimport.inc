<?php

function spwebformimport_detect_delimiter($file) {
  $realpath = drupal_realpath($file->uri);
  $found = FALSE;
  $file_object = new SplFileObject($realpath);
  $delimiters = ["\t", ";", "|", ","];
  $data_1 = null; $data_2 = null;
  $delimiter = $delimiters[0];
  foreach($delimiters as $d) {
    $data_1 = $file_object->fgetcsv($d);
    if (count($data_1) > 1) {
      if(sizeof($data_1) > sizeof($data_2)) {
        $found = TRUE;
        $delimiter = sizeof($data_1) > sizeof($data_2) ? $d : $delimiter;
        $data_2 = $data_1;
      }
      $file_object->rewind();
    }
  }
  if ($found) {
    return $delimiter;
  }
  return FALSE;
}

function spwebformimport_parse_csv_field_mapping_data($file, $delimiter) {
  $data = array();
  $realpath = drupal_realpath($file->uri);
  if (($fh = fopen($realpath, 'r'))) {
    for ($row_number = 0; $row_number < 5; $row_number++) {
      $row = fgetcsv($fh, 4096, $delimiter);
      if ($row) {
        foreach ($row as $column_number => $value) {
          $data[$column_number][$row_number] = '&bull;&nbsp;' . $value;
        }
      }
    }
    return $data;
  }
  return FALSE;
}


function spwebformimport_get_import_components($node) {
  $import_components = array();
  foreach ($node->webform['components'] as $cid => $component) {
    if ($component['type'] == 'file' || $component['type'] == 'fieldset' || $component['type'] == 'markup' || $component['type'] == 'pagebreak') {
      continue;
    }
    $import_components[$cid] = $component;
  }
  return $import_components;
}

function spwebformimport_batch($selected_import_components, $webform, $file, $delimiter, &$context) {
  // Process by chunks.
  $chunk_size = 50;

  $realpath = drupal_realpath($file->uri);
  $file_object = new SplFileObject($realpath);

  if (empty($context['sandbox'])) {
    // Get number of rows to import.
    $file_object->seek(PHP_INT_MAX);
    $rowcount = $file_object->key();
    $file_object->rewind();

    $context['sandbox'] = array();
    $context['sandbox']['progress'] = 0;
    $context['sandbox']['current_row'] = 0;
    $context['sandbox']['max'] = $rowcount;
    $context['sandbox']['current_chunk'] = 0;
    $context['results']['count'] = 0;
  }

  // Get rows.
  $rows = array();
  $file_object->seek($context['sandbox']['progress']);
  for ($i = 0; $i < $chunk_size; $i++) {
    $row = $file_object->fgetcsv($delimiter);
    if ($file_object->eof()) {
      $context['finished'] = 1;
      return;
      break;
    }
    $result = spwebformimport_batch_process_row($row, $selected_import_components, $webform);
    if ($result) {
      $context['results']['count'] ++;
    }
    // Update our progress information.
    $context['sandbox']['progress'] = $file_object->key();
  }

  $context['sandbox']['current_chunk'] ++;
  $context['message'] = ($context['sandbox']['current_chunk'] * $chunk_size) . '/' . $context['sandbox']['max'];
  $context['finished'] = ($context['sandbox']['progress'] / $context['sandbox']['max']);
}

function spwebformimport_batch_process_row($row, $selected_import_components, $webform) {
  global $user;
  $components = $webform->webform['components'];

  $count = 0;
  foreach ($selected_import_components as $csv_field_nr => $selected_component_info) {
    $selected_component = $components[$selected_component_info['component_id']];
    $count++;

    $value = spwebformimport_csvfieldtrim($row[$csv_field_nr]);

    // Check if component is required, and has value, else, skip row.
    if ($value == '') {
      if ($selected_component['required']) {
        drupal_set_message('Verplichte veldwaarde (nr. ' . $csv_field_nr . ') ontbreekt, rij (nr. ' . $count . ') wordt overgeslagen.', 'warning');
        return;
      }
      else {
        continue;
      }
    }

    switch ($selected_component['type']) {
    case 'time':
    case 'date':
      if ($time = strtotime($value)) {
        $value = $selected_component['component_type'] = 'time' ? date('H:i:s', $time) : date('Y-m-d', $time);
      }
      else {
        drupal_set_message('Ongeldige tijd/datum veldwaarde (nr. ' . $csv_field_nr . '), rij (nr. ' . $count . ') wordt overgeslagen.', 'warning');
        return;
      }
      break;
    case 'select':
      if ($selected_component['extra']['multiple']) {
        $multiselect_values = array();
        foreach ($selected_component_info['select_mapping'] as $webform_key => $csv_value) {
          if (strpos($value, $csv_value) !== FALSE) {
            $multiselect_values[] = $webform_key;
          }
        }
        if (empty($multiselect_values)) {
          continue 2;
        }
        $value = $multiselect_values;
      }
      else {
        $select_value = '';
        foreach ($selected_component_info['select_mapping'] as $webform_key => $csv_value) {
          if (strpos($value, $csv_value) !== FALSE) {
            $select_value = $webform_key;
            break;
          }
        }
        if (empty($select_value)) {
          continue 2;
        }
        $value = $select_value;
      }
      break;
    case 'grid':
      $questions = array_keys(spwebformimport_parse_select_options($webform, $selected_component['cid'], 'questions'));
      $csv_values = explode(',', $value);
      $value = array();
      foreach ($csv_values as $key => $csv_value) {
        $value[$questions[$key]] = $csv_value;
      }
      break;
    }
    $webform_data[$selected_component['cid']] = $value;
  }
  $submission = (object) array(
    'nid' => $webform->nid,
    'uid' => $user->uid,
    'submitted' => time(),
    'remote_addr' => ip_address(),
    'is_draft' => 0,
    'data' => webform_submission_data($webform, $webform_data),
  );
  $sids[] = webform_submission_insert($webform, $submission);
  return TRUE;
}

function spwebformimport_batch_finished($success, $results, $operations) {
  if ($success) {
    drupal_set_message('Aantal opgehaalde contacten: ' . $results['count']);
  }
}

function spwebformimport_csvfieldtrim($value) {
  $value = trim($value);
  // Strip off the beginning and ending quotes if necessary.
  $value = trim($value,'"');
  return $value;
}

function spwebformimport_parse_select_options($webform, $selected_component, $type) {
  $options = array();
  if (!empty($webform->webform['components'][$selected_component]['extra'][$type])) {
    $items = $webform->webform['components'][$selected_component]['extra'][$type];
    $lines = explode("\n", trim($items));
    foreach ($lines as $line) {
      $line = trim($line);
      $matches = array();
      if (preg_match('/^([^|]*)\|(.*)$/', $line, $matches)) {
        $options[$matches[1]] = empty($matches[2]) ? $matches[1] : $matches[2];
      }
    }
  }
  return $options;
}
