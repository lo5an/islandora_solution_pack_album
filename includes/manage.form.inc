<?php

/**
 * @file
 * Definitely not cribbed from compound's includes/manage.form.inc ha ha.
 */

/**
 * Menu callback for manage form.
 */
function islandora_album_manage_tracklist(FedoraObject $object) {
  return drupal_get_form('islandora_album_manage_tracklist_form', $object);
}

/**
 * Album management form.
 */
function islandora_album_manage_tracklist_form($form, &$form_state, $object) {
  $form = array();
  $form_state['object'] = $object;
  $rels_predicate = 'isTrackOf';

  // Add child objects.
  if (in_array('islandora:albumCModel', $object->models)) {
    $form['add_tracks'] = array(
      '#type' => 'fieldset',
      '#title' => t('Add Tracks'),
      '#description' => t('Add tracks to this album.'),
    );
    $form['add_tracks']['track'] = array(
      '#type' => 'textfield',
      '#title' => t('Child Object Pid/Label'),
    );

    // Remove children.
    $children = islandora_album_get_track_pids($object->id);
    if (!empty($children)) {
      $form['tracks'] = array(
        '#type' => 'fieldset',
        '#title' => t('Remove Tracks'),
        '#description' => t('Remove tracks from this album.'),
        '#collapsed' => TRUE,
        '#collapsible' => TRUE,
      );

      $header = array('title' => t('Title'), 'pid' => t('Object ID'));
      $form['tracks']['remove_track'] = array(
        '#type' => 'tableselect',
        '#title' => t('Tracks'),
        '#header' => $header,
        '#options' => $children,
      );
      $form['reorder_fieldset'] = array(
        '#type' => 'fieldset',
        '#title' => t('Reorder'),
        '#collapsed' => TRUE,
        '#collapsible' => TRUE,
      );
      $form['reorder_fieldset']['table'] = array(
        '#theme' => 'islandora_compound_object_draggable_table',
      ) + islandora_album_get_tabledrag_element($object);
    }
  }

  $form['object'] = array(
    '#type' => 'value',
    '#value' => $object,
  );

  $form['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Save'),
  );

  return $form;
}

/**
 * Album management validation.
 */
function islandora_album_manage_tracklist_form_validate($form, &$form_state) {
  module_load_include('inc', 'islandora', 'includes/utilities');
  $object = $form_state['values']['object'];

  if (!empty($form_state['values']['track'])) {
    // Make sure the PID is valid.
    if (!islandora_is_valid_pid($form_state['values']['track'])) {
      form_set_error('track', t('Invalid object supplied.'));
    }

    // Do not allow repeated child.
    $track = islandora_object_load($form_state['values']['track']);
    if (!$track) {
      form_set_error('track', t('The PID given does not represent an object.'));
    }
    else {
      if (!in_array('islandora:sp-audioCModel', $track->models)) {
        form_set_error('track', t('The given object is not an islandora:sp-audioCModel object.'));
      }
      else {
        $track_part_of = $track->relationships->get(FEDORA_RELS_EXT_URI, 'isTrackOf');
        foreach ($track_part_of as $part) {
          if ($part['object']['value'] == $object->id) {
            form_set_error('child', t('The object is already a parent of the child.'));
          }
        }
      }
    }
  }
}

/**
 * Tracklist management submit.
 */
function islandora_album_manage_tracklist_form_submit($form, &$form_state) {
  $object = $form_state['values']['object'];

  // Relationship from track to album.
  if (!empty($form_state['values']['track'])) {
    $track = islandora_object_load($form_state['values']['track']);
    islandora_album_add_parent($track, $object->id);
  }

  // Remove children.
  if (!empty($form_state['values']['remove_track'])) {
    islandora_album_remove_parent(array_map('islandora_object_load', array_filter($form_state['values']['remove_track'])), $object->id);
  }
  drupal_set_message(t('Track relationships modified.'));
}

/**
 * Returns the table to be used for tabledragging.
 *
 * @param AbstractObject $object
 *   An AbstractObject representing an object within Fedora.
 *
 * @return array
 *   An array defining the markup for the table.
 */
function islandora_album_get_tabledrag_element(AbstractObject $object) {
  $tracks = islandora_album_get_track_pids($object->id);
  $seq = 0;
  $delta = count($tracks);
  $map = function($child) use($delta, &$seq) {
    $seq++;
    return array(
      'label' => array('#markup' => check_plain($child['title'])),
      'pid' => array('#markup' => check_plain($child['pid'])),
      'weight' => array(
        '#type' => 'weight',
        '#title' => t('Weight'),
        '#default_value' => $seq,
        '#delta' => $delta,
        '#title-display' => 'invisible',
      ));
  };
  $rows = array_map($map, $tracks);

  return array(
    '#tree' => TRUE,
    'table' => array(
      '#type' => 'markup',
      '#header' => array(t('Title'), t('Object ID'), t('Weight')),
      'rows' => $rows,
    ),
    'actions' => array(
      '#type' => 'actions',
      'submit' => array(
        '#type' => 'submit',
        '#value' => t('Save Changes'),
        '#submit' => array('islandora_album_sequence_submit'),
      ),
    ),
  );
}

/**
 * Submit handler for the re-ordering table.
 *
 * @param array $form
 *   The Drupal form.
 * @param array $form_state
 *   The Drupal form state.
 */
function islandora_album_sequence_submit($form, &$form_state) {
  $object = $form_state['object'];
  $tracks = &$form_state['values']['table']['table']['rows'];
  if ($tracks) {
    uasort($tracks, 'drupal_sort_weight');
    islandora_album_sequence_batch($object->id, $tracks);
  }
}

/**
 * Defines and constructs the batch used to re-order tracks.
 *
 * @param string $pid
 *   The pid of the parent object.
 * @param array $tracks
 *   An array of tracks we are iterating over.
 */
function islandora_compound_object_sequence_batch($pid, array $tracks) {
  $object = islandora_object_load($pid);
  $context['results'] = array(
    'success' => array(),
    'failure' => array(),
  );
  $context['sandbox']['progress'] = 0;
  $context['sandbox']['total'] = count($tracks);
  $operations = islandora_album_build_sequence_operations($pid, $tracks);
  $batch = array(
    'operations' => $operations,
    'title' => t("Sequencing @label's tracks ...", array('@label' => $object->label)),
    'init_message' => t("Preparing to sequence @label's tracks ...", array('@label' => $object->label)),
    'progress_message' => t('Time elapsed: @elapsed <br/>Estimated time remaining @estimate.'),
    'error_message' => t('An error has occurred.'),
    'file' => drupal_get_path('module', 'islandora_album') . '/includes/manage.form.inc',
  );
  batch_set($batch);
}

/**
 * Builds up a sequence of operations for sequence numbering.
 *
 * @param string $album_pid
 *   The pid of the album.
 * @param array $tracks
 *   An array of tracks.
 *
 * @return array
 *   The built up operations array.
 */
function islandora_album_build_sequence_operations($album_pid, $tracks) {
  $operations = array();
  $start_seq = 1;
  foreach ($tracks as $child_pid => $weight) {
    $operations[] = array(
      'islandora_album_update_sequence', array(
        $album_pid,
        $child_pid,
        $start_seq,
      ),
    );
    $start_seq++;
  }
  return $operations;
}

/**
 * Updates the relationships to reflect the object's track number on the album.
 *
 * @param string $album_pid
 *   The pid of the album.
 * @param array $track_pid
 *   The pid of the track;
 * @param array $context
 *   Context used in the batch.
 */
function islandora_album_update_sequence($album_pid, $track_pid, $start_seq, &$context) {
  $escaped_pid = str_replace(':', '_', $album_pid);
  $track = islandora_object_load($track_pid);
  $track->relationships->remove(ISLANDORA_RELS_EXT_URI, "isTrackNumberOf$escaped_pid");
  $track->relationships->add(ISLANDORA_RELS_EXT_URI, "isTrackNumberOf$escaped_pid", $start_seq, RELS_TYPE_PLAIN_LITERAL);
  $context['message'] = t('Inserting track "@label" (@pid) at position "@pos"', array(
    '@label' => $track->label,
    '@pid' => $track->id,
    '@pos' => $start_seq,
    )
  );
}

/**
 * Retrieves the insertion sequence number point.
 *
 * @param array $children
 *   All the child objects of a parent we need to iterate over.
 *
 * @return int
 *   The point at which we are to insert the compound object.
 */
function islandora_album_get_insert_sequence_number($children) {
  $insert_seq = 0;
  foreach ($children as $child) {
    if (!empty($child['track']) && $child['track'] > $insert_seq) {
      $insert_seq = $child['track'];
    }
  }
  // Want to insert one past this point.
  $insert_seq++;
  return $insert_seq;
}

/**
 * Add the given tracks to the given albums.
 *
 * @param array|AbstractObject $tracks
 *   An array of AbstractObject tracks to add to albums.
 * @param array|string $album_pids
 *   An array of PIDs representing the albums to which a track should be
 *   added, to make each member of $tracks a member of multiple albums.
 */
function islandora_album_add_parent($tracks, $album_pids) {
  $tracks = $tracks instanceof AbstractObject ? array($tracks) : $tracks;
  $album_pids = (array) $album_pids;

  foreach ($album_pids as $album_pid) {
    $escaped_pid = str_replace(':', '_', $album_pid);

    foreach ($tracks as $track) {
      $children = islandora_album_get_track_pids($album_pid);
      $insert_seq = islandora_album_get_insert_sequence_number($children);

      $track->relationships->autoCommit = FALSE;
      $track->relationships->add(FEDORA_RELS_EXT_URI, 'isTrackOf', $album_pid);
      $track->relationships->add(ISLANDORA_RELS_EXT_URI, "isTrackNumberOf$escaped_pid", $insert_seq, RELS_TYPE_PLAIN_LITERAL);
      $track->relationships->commitRelationships();
    }
  }
}

/**
 * Remove the given objects from the given parents.
 *
 * @param array|string $objects
 *   An array of objects to remove from each of the PIDs listed in
 *   $parent_pids.
 * @param array|string $parent_pids
 *   An array of PIDs from which the members of $objects will be removed.
 */
function islandora_album_remove_parent($objects, $parent_pids) {
  $objects = $objects instanceof AbstractObject ? array($objects) : $objects;
  $parent_pids = (array) $parent_pids;

  foreach ($parent_pids as $parent_pid) {
    $escaped_pid = str_replace(':', '_', $parent_pid);

    foreach ($objects as $object) {
      $object->relationships->remove(FEDORA_RELS_EXT_URI, 'isTrackOf', $parent_pid);
      $object->relationships->remove(ISLANDORA_RELS_EXT_URI, "isTrackNumberOf$escaped_pid");
    }
  }
}
