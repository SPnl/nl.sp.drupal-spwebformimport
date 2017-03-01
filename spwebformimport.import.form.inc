<?php

function spwebformimport_import_form($form, &$form_state) {
  $values = (empty($form_state['values']) ? array() : $form_state['values']);
  if (!empty($form_state['storage'])) {
    $storage = &$form_state['storage'];
  }
  else {
    $storage = array();
  }
  //dpm($values);
  //dpm($storage);

  $form = array();
  $form['#attached']['css'] = array(
    drupal_get_path('module', 'spwebformimport') . '/webformimport.css',
  );

  $form['#prefix'] = '<div id="spwebformimport_ajax_form">';
  $form['#suffix'] = '</div>';

  if (empty($storage['upload'])) {
    // Set the encoding type (necessary for file uploads).
    $form['#attributes']['enctype'] = 'multipart/form-data';
  }

  // Select webform element.
  // ----------------------------------------------------------
  $webforms = spwebformsync_get_webforms();
  $webform_select_options = array();
  foreach ($webforms as $webform) {
    $webform_select_options[$webform->nid] = $webform->title;
  }
  if (!empty($webform_select_options)) {

    // Create form element.
    $form['webform'] = array(
      '#type' => 'select',
      '#title' => 'Kies het webformulier waar de data heen moet',
      '#options' => $webform_select_options,
      '#empty_option' => 'Kies:',
      '#name' => 'webform',
      '#ajax' => array(
        'wrapper' => 'spwebformimport_ajax_form',
        'callback' => 'spwebformimport_ajax_form_callback',
      ),
    );

    if (!empty($storage['webform'])) {

      // Upload file.
      // ----------------------------------------------------------
      if (empty($storage['upload'])) {
        $form['upload'] = array(
          '#type' => 'file',
          '#title' => t('Delimited data file'),
          '#description' => t('Choose the data file containing the data you want uploaded.'),
          '#disabled' => (empty($storage['upload'])) ? FALSE : TRUE,
        );

        $form['submit_file'] = array(
          '#type' => 'submit',
          '#name' => 'submit_file',
          '#value' => 'Verder',
          '#ajax' => array(
            'wrapper' => 'spwebformimport_ajax_form',
            'callback' => 'spwebformimport_ajax_form_callback',
          ),
        );
      }
      else {
        if ($storage['delimiter'] !== FALSE) {
          $delimiter = ($storage['delimiter'] === "\t") ? 'tab' : $storage['delimiter'];
          if (!empty($delimiter)) {
            $form['selected_file'] = array(
              '#markup' => '<p>Bestand: ' . $storage['upload']->filename . '<br/>' . 'Veldscheidingsteken: "' . $delimiter . '"</p>',
            );
          }
        }
        // Map fields.
        // ----------------------------------------------------------
        $form['mapping'] = array(
          '#type' => 'fieldset',
          '#title' => 'Mapping',
          '#description' => '<p>Webformulier componenten met een * zijn verplicht.</p>',
        );
        $form['mapping']['table_start'] = array(
          '#markup' => '<table><tr><th>csv veld waarden</th><th>webformulier component</th><th>selectiewaarde mapping</th>',
        );
        foreach ($storage['csv_field_mapping_data'] as $csv_field_key => $example_values) {
          $form['mapping']['csv_field_key_' . $csv_field_key . '_component'] = array(
            '#type' => 'select',
            '#options' => $storage['components_list'],
            '#empty_option' => '- Geen -',
            '#name' => 'csv_field_key_' . $csv_field_key . '_component',
            '#prefix' => '<tr><td><p>' . implode('<br/>', $example_values) . '</p></td><td>',
            '#suffix' => '</td>',
            '#ajax' => array(
              'wrapper' => 'spwebformimport_ajax_form',
              'callback' => 'spwebformimport_ajax_form_callback',
            ),
          );
          // Add select component mapping.
          if (
            !empty($storage['selected_import_components'][$csv_field_key]['component_id']) &&
            $storage['import_components'][$storage['selected_import_components'][$csv_field_key]['component_id']]['type'] === 'select'
          ) {
            $form['mapping']['select_component_mapping_start_' . $csv_field_key] = array(
              '#markup' => '<td>',
            );
            $component_id = $storage['selected_import_components'][$csv_field_key]['component_id'];
            $component_options = spwebformimport_parse_select_options($storage['webform_node'], $component_id, 'items'); 
            foreach ($component_options as $component_option_key => $component_option_title) {
              $form['mapping']['select_component_mapping_' .$csv_field_key . '_' . $component_option_key] = array(
                '#type' => 'textfield',
                '#title' => $component_option_title . ': csv veld bevat',
              );
            }
            $form['mapping']['select_component_mapping_end_' . $csv_field_key] = array(
              '#markup' => '</td></tr>',
            );
          }
          else {
            $form['mapping']['select_component_mapping_' . $csv_field_key] = array(
              '#markup' => '<td></td>',
            );
          }

          $form['submit_mapping'] = array(
            '#type' => 'submit',
            '#name' => 'submit_mapping',
            '#value' => 'Importeer',
          );
        }
        $form['mapping']['table_end'] = array(
          '#markup' => '</table>',
        );
      }
    }
  }
  else {
    $form['no_webforms'] = array(
      '#markup' => 'Er is geen webformulier beschikbaar (<a href="/node/webform/add">voeg toe</a>).',
    );
  }
  return $form;
}

function spwebformimport_ajax_form_callback($form, &$form_state) {
  return $form;
}

function spwebformimport_import_form_validate($form, &$form_state) {
  $storage = &$form_state['storage'];                                                                 
  $values = $form_state['values'];

  if (!empty($form_state['triggering_element']['#name'])) {                                           
    $trigger = $form_state['triggering_element']['#name'];
    switch ($trigger) {
    case 'webform':
      $webform = node_load($values['webform']);
      if (isset($storage['webform'])) {
        $storage = array();
      }
      $storage['webform'] = $values['webform'];
      $storage['webform_node'] = $webform;
      return;
      break;
    case 'submit_file':
      $file = file_save_upload('upload', array('file_validate_extensions' => array('csv tsv txt')));
      if ($file) {
        // Move the file into the Drupal file system.
        if ($file = file_move($file, 'private://')) {
          // Get delimiter.
          $delimiter = spwebformimport_detect_delimiter($file);
          if ($delimiter === FALSE) {
            form_set_error('file', 'Het veldscheidingsteken kon niet worden gedetecteerd.');
          }
          else {
            // Save the file for use in the submit handler.
            $storage['upload'] = $file;
            $storage['delimiter'] = $delimiter;
            // Save csv field data
            $storage['csv_field_mapping_data'] = spwebformimport_parse_csv_field_mapping_data($file, $delimiter);
            // Get import components.
            $storage['import_components'] = spwebformimport_get_import_components($storage['webform_node']);
            $storage['components_list'] = spwebformsync_get_components_list($storage['import_components']);
            foreach ($storage['components_list'] as $component_key => $component_title) {
              if ($storage['webform_node']->webform['components'][$component_key]['required']) {
                $storage['components_list'][$component_key] = '* ' . $component_title;
              }
            }
          }
        }
        else {
          form_set_error('file', t("Failed to write the uploaded file to the site's file folder."));
        }
      }
      else {
        form_set_error('file', t('No file was uploaded.'));
      }
      return;
      break;
    case 'submit_mapping':
      // Check if required webform fields are set.
      foreach ($storage['selected_import_components'] as $csv_field_key => $selected_component) {
        $selected_components[$csv_field_key] = $selected_component['component_id'];
      }
      foreach ($storage['webform_node']->webform['components'] as $component_id => $component) {
        if ($component['required']) {
          if (!in_array($component_id, $selected_components)) {
            form_set_error('mapping_error_' . $component_id, 'Het webformulier component "' . $component['name'] . '" is een verplicht veld!'); 
          }
        }
      }
      return;
      break;
    }

    // Component selection field.
    preg_match('/^csv_field_key_([0-9]+)_component$/', $trigger, $matches);
    if (!empty($matches[0])) {
      if (empty($values[$matches[0]])) {
        unset($storage['selected_import_components'][$matches[1]]);
      }
      else {
        // Store needed component info.
        $storage['selected_import_components'][$matches[1]]['component_id'] = $values[$matches[0]];
      }
    }
  }
}

function spwebformimport_import_form_submit($form, &$form_state) {
  $storage = &$form_state['storage'];                                                                 
  $values = $form_state['values'];

  if (!empty($form_state['triggering_element']['#name'])) {                                           
    $trigger = $form_state['triggering_element']['#name'];
    switch ($trigger) {
    case 'submit_file':
      //$file = $storage['upload'];
      //$file->status = FILE_STATUS_PERMANENT;
      //file_save($file);

      $form_state['rebuild'] = TRUE;
      return;
      break;
    case 'submit_mapping':
      // Store select component mapping.
      foreach ($values as $key => $value) {
        preg_match('/^select_component_mapping_([0-9]+)_([A-Za-z_]+)$/', $key, $matches);
        if (!empty($matches[0])) {
          if (empty($values[$matches[0]])) {
            unset($storage['selected_import_components'][$matches[1]]['select_mapping'][$matches[2]]);
          } 
          else {
            $storage['selected_import_components'][$matches[1]]['select_mapping'][$matches[2]] = $values[$matches[0]];
          } 
        }
      }
      // Import csv.
      $batch = array(                                                                             
        'operations' => array(
          array('spwebformimport_batch', array($storage['selected_import_components'], $storage['webform_node'], $storage['upload'], $storage['delimiter'])),                                                              
        ),
        'finished' => 'spwebformimport_batch_finished',                                      
        'title' => 'Importeren csv',                                                           
        'init_message' => '0',                                                                    
        'progress_message' => '',                                        
        'error_message' => 'Er is een fout opgetreden bij het ophalen van data',             
      );                                                                                          
      batch_set($batch); 
      return;
      break;
    }
  }
}

/**
 * Returns a list of value delimiters we can use.
 *
 * @return
 *   An array of key/value pairs for form options list.
 */
function _spwebformimport_delimiter_options() {                                                        
  return array(
    ',' => t('Comma (,)'),
    '\t' => t('Tab (\t)'),
    ';' => t('Semicolon (;)'),
    ':' => t('Colon (:)'),
    '|' => t('Pipe (|)'),
    '.' => t('Period (.)'),
    ' ' => t('Space ( )'),
  );
}
