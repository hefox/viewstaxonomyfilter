<?php

/**
 * Filter by term id
 */
class views_handler_filter_term_node_all_tid extends views_handler_filter_many_to_one {
  function has_extra_options() { return TRUE; }

  function get_value_options() { /* don't overwrite the value options */ }

  function option_definition() {
    $options = parent::option_definition();

    $options['type'] = array('default' => 'select');
    $options['limit'] = array('default' => TRUE);
    $options['vid'] = array('default' => array(0));

    return $options;
  }

  function extra_options_form(&$form, &$form_state) {
    $vocabularies = taxonomy_get_vocabularies();
    foreach ($vocabularies as $voc) {
      $options[$voc->vid] = check_plain($voc->name);
    }

    if ($this->options['limit']) {
      // We only do this when the form is displayed so this query doesn't run
      // unnecessarily just when the object is constructed.
      if (!array_filter($this->options['vid'])) {
        $this->options['vid'] = array(db_result(db_query('SELECT MIN(vid) FROM {vocabulary} v')));
      }

      $form['vid'] = array(
        '#prefix' => '<div class="views-left-40">',
        '#suffix' => '</div>',
        '#type' => 'checkboxes',
        '#title' => t('Vocabulary'),
        '#multiple' => TRUE,
        '#options' => $options,
        '#description' => t('Select which vocabularies to show terms for in the regular options.'),
        '#default_value' => $this->options['vid'],
      );
    }

    $form['markup_start'] = array(
      '#value' => '<div class="views-left-40">',
    );

    $form['type'] = array(
      '#type' => 'radios',
      '#title' => t('Selection type'),
      '#options' => array('select' => t('Dropdown'), 'textfield' => t('Autocomplete'),'cloud' => t('Cloud'), 'faceted' => t('Faceted')),
      '#default_value' => $this->options['type'],
    );

    $form['hierarchy'] = array(
      '#type' => 'checkbox',
      '#title' => t('Show hierarchy in dropdown'),
      '#default_value' => !empty($this->options['hierarchy']),
      '#process' => array('views_process_dependency'),
      '#dependency' => array('radio:options[type]' => array('select')),
    );

    $form['markup_end'] = array(
      '#value' => '</div>',
    );
  }

  function value_form(&$form, &$form_state) {
    static $view_running = 0;
    foreach($this->options['vid'] as $vid) if ($vid) $vocabularies[$vid] = taxonomy_vocabulary_load($vid);
    if (empty($vocabularies) && $this->options['limit']) {
      $form['markup'] = array(
        '#prefix' => '<div class="form-item">',
        '#suffix' => '</div>',
        '#value' => t('An invalid vocabulary is selected. Please change it in the options.'),
      );
      return;
    }

    if ($this->options['type'] == 'cloud' || $this->options['type'] == 'faceted')  {     
      
      $view = $this->view;
      
      if (empty($view_running)) {
      $default_value = (array) $this->value; 
          $args = $view->args;
          $counts = array();
          drupal_alter('viewstaxonomyfilter_cloud_arg',$args,$view); //cause exposed blocks won't have you args necessarily 
          $view_running = 1; 
          $nids = array();
          $view_args = array();
          $view2 =  $view->clone_view();
          $view2->is_cacheable = FALSE;
          $view2->set_items_per_page(0);
          $view2->exposed_input = $view->exposed_input;
          $view2->args = $args;
          $view2->execute_display($view->current_display);
          
          if (is_array($view2->result))  foreach ($view2->result as $val) $nids[] =$val->nid;
          if ($nids) {
            $qargs = $nids;
            if ($this->options['limit']) {
              $vids = array_keys($vocabularies);
              $qargs = array_merge($vids,$qargs);
              $limitvid = "d.vid in (". db_placeholders($vids) .") and";
            }
          
              $result = db_query("SELECT d.*, count(distinct nid) as count from {term_data} d join {term_node} n on n.tid = d.tid where ".$limitvid." nid in (".db_placeholders($nids).") group by tid",$qargs);
            while($row = db_fetch_object($result)) {
              $counts[$row->tid] = $row;
              $vocab[$row->vid][$row->tid] = $row;
            }
            $id = $this->options['expose']['identifier'] ;
          }
          $view_running = 0; 
        }

        if ($counts) {
        $querystring = drupal_query_string_encode($_REQUEST, array_merge(array('q', 'pass'), array_keys($_COOKIE)));
          foreach($counts as $tid => $term) {
            $parameters = array($id =>array($tid=>$tid));
            $query = array();
            $query[] = drupal_query_string_encode($parameters, array());
            if ($querystring != '') {
              $query[] = $querystring;
            }
            $queries[$tid] = count($query) ? implode('&', $query) : NULL;
            
          }
        }
      if ($vocab) {  
        switch ($this->options['type']) {
          case 'cloud':
            
          case 'faceted':
            foreach($vocab as $vid => $terms) {
              
              $output .= '<h3>'. $vocabularies[$vid]->name . '</h3>'; 
              $vl = array();
              foreach($terms as $tid => $term) {
                $vl[] = array('title'=>$term->name,'href'=>$_GET['q'], 'query'=>$queries[$tid]);  
              }
              $output .= theme('links',$vl);
            }
          break;
        }
        $form['visiable_value'] = array(
          '#type' => 'markup',
          '#value' => $output,
        );
      }
      $hide = true;
    }
    
    if ($this->options['type'] == 'textfield') {
      $default = '';
      if ($this->value) {
        $result = db_query("SELECT * FROM {term_data} td WHERE td.tid IN (" . implode(', ', $this->value) . ')');
        while ($term = db_fetch_object($result)) {
          if ($default) {
            $default .= ', ';
          }
          $default .= $term->name;
        }
      }
      $form['value'] = array(
        '#title' => $this->options['limit'] ? t('Select terms from vocabulary @voc', array('@voc' => $vocabulary->name)) : t('Select terms'),
        '#type' => 'textfield',
        '#default_value' => $default,
      );

      if ($this->options['limit']) {
        $form['value']['#autocomplete_path'] = 'taxonomy/autocomplete/' . implode("+",array_keys($vocabularies));
      }
    }
    else {
      if (!empty($this->options['hierarchy']) && $this->options['limit']) {
        $tree = array();
        foreach(array_keys($vocabularies) as $vid) {
            $tree = array_merge($tree, taxonomy_get_tree($vid));
        }
        $options = array();

        if ($tree) {
          foreach ($tree as $term) {
            $choice = new stdClass();
            $choice->option = array($term->tid => str_repeat('-', $term->depth) . $term->name);
            $options[] = $choice;
          }
        }
      }
      else {
        $options = array();
        if ($this->options['limit']) {
            $vids = array_keys($vocabularies);
          $result = db_query("SELECT * FROM {term_data} WHERE vid in (". db_placeholders($vids) .") ORDER BY weight, name", $vids);
        }
        else {
          $result = db_query("SELECT td.* FROM {term_data} td INNER JOIN {vocabulary} v ON td.vid = v.vid ORDER BY v.weight, v.name, td.weight, td.name");
        }
        while ($term = db_fetch_object($result)) {
          $options[$term->tid] = $term->name;
        }
      }

      $default_value = (array) $this->value;

      if (!empty($form_state['exposed'])) {
        $identifier = $this->options['expose']['identifier'];

        if (!empty($this->options['expose']['reduce'])) {
          $options = $this->reduce_value_options($options);

          if (empty($this->options['expose']['single']) && !empty($this->options['expose']['optional'])) {
            $default_value = array();
          }
        }

        if (!empty($this->options['expose']['single'])) {
          if (!empty($this->options['expose']['optional']) && (empty($default_value) || !empty($this->options['expose']['reduce']))) {
            $default_value = 'All';
          }
          else if (empty($default_value)) {
            $keys = array_keys($options);
            $default_value = array_shift($keys);
          }
          else {
            $copy = $default_value;
            $default_value = array_shift($copy);
          }
        }
      }
      $default_value = array_filter($default_value);
      $form['value'] = array(
        '#type' => 'select',
        '#title' => t('Select terms'),
        '#multiple' => TRUE,
        '#options' => $options,
        '#size' => min(9, count($options)),
        '#default_value' => array_filter($default_value),
      );
      if ($hide) { // seriously, wtf why?
        $form['value']['#prefix'] = '<div style="display:none;">' ;
         $form['value']['#suffix'] = '</div>' ;
      }
      if (!empty($form_state['exposed']) && !isset($form_state['input'][$identifier])) {
        $form_state['input'][$identifier] = $default_value;
      }
    }


    if (empty($form_state['exposed'])) {
      // Retain the helper option
      $this->helper->options_form($form, $form_state);
    }
  }

  function value_validate(&$form, &$form_state) {
    // We only validate if they've chosen the text field style.
    if ($this->options['type'] != 'textfield') {
      return;
    }

    $values = drupal_explode_tags($form_state['values']['options']['value']);
    $tids = $this->validate_term_strings($form['value'], $values);

    if ($tids) {
      $form_state['values']['options']['value'] = $tids;
    }
  }

  function accept_exposed_input($input) {
    if (empty($this->options['exposed'])) {
      return TRUE;
    }

    // If it's optional and there's no value don't bother filtering.
    if ($this->options['expose']['optional'] && empty($this->validated_exposed_input)) {
      return FALSE;
    }

    $rc = parent::accept_exposed_input($input);
    if ($rc) {
      // If we have previously validated input, override.
      if (isset($this->validated_exposed_input)) {
        $this->value = $this->validated_exposed_input;
      }
    }

    return $rc;
  }

  function exposed_validate(&$form, &$form_state) {
    if (empty($this->options['exposed'])) {
      return;
    }

    $identifier = $this->options['expose']['identifier'];

    // We only validate if they've chosen the text field style.
    if ($this->options['type'] != 'textfield') {
      if ($form_state['values'][$identifier] != 'All')  {
        $this->validated_exposed_input = (array) $form_state['values'][$identifier];
        if (!$this->validated_exposed_input && $_GET[$identifier]) 
          $this->validated_exposed_input  = (array) $_GET[$identifier]; 
      }
      return;
    }

    if (empty($this->options['expose']['identifier'])) {
      return;
    }

    $values = drupal_explode_tags($form_state['values'][$identifier]);
  
    $tids = $this->validate_term_strings($form[$identifier], $values);
    if ($tids) {
      $this->validated_exposed_input = $tids;
    }
  }

  /**
   * Validate the user string. Since this can come from either the form
   * or the exposed filter, this is abstracted out a bit so it can
   * handle the multiple input sources.
   */
  function validate_term_strings(&$form, $values) {
    if (empty($values)) {
      return array();
    }

    $tids = array();
    $placeholders = array();
    $args = array();
    $results = array();
    foreach ($values as $value) {
      $missing[strtolower($value)] = TRUE;
      $args[] = $value;
      $placeholders[] = "'%s'";
    }

    if (!$args) {
      return;
    }

    // add the taxonomy vid to the argument list.
    $args =array_merge($args, $this->options['vid']);

    $result = db_query("SELECT * FROM {term_data} WHERE name IN (" . implode(', ', $placeholders) . ") AND vid in (". implode(', ', $this->options['vid']) .")", $args);
    while ($term = db_fetch_object($result)) {
      unset($missing[strtolower($term->name)]);
      $tids[] = $term->tid;
    }

    if ($missing) {
      form_error($form, format_plural(count($missing), 'Unable to find term: @terms', 'Unable to find terms: @terms', array('@terms' => implode(', ', array_keys($missing)))));
    }

    return $tids;
  }

  function value_submit($form, &$form_state) {
    // prevent array_filter from messing up our arrays in parent submit.
  }

  function expose_form_right(&$form, &$form_state) {
    parent::expose_form_right($form, $form_state);
    if ($this->options['type'] != 'select') {
      unset($form['expose']['reduce']);
    }
  }

  function admin_summary() {
    // set up $this->value_options for the parent summary
    $this->value_options = array();

    if ($this->value) {
      $result = db_query("SELECT * FROM {term_data} td WHERE td.tid IN ("  . implode(', ', $this->value) . ")");

      while ($term = db_fetch_object($result)) {
        $this->value_options[$term->tid] = $term->name;
      }
    }
    return parent::admin_summary();
  }
}
