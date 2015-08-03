<?php

/**
 * @file
 * Lyrics editing form.
 */

function islandora_album_edit_lyrics(AbstractObject $object) {
  return drupal_get_form('islandora_album_edit_lyrics_form', $object);
}

function islandora_album_edit_lyrics_form($form, &$form_state, AbstractObject $object) {
  $lyrics = $object['LYRICS']->content;
  return array(
    'lyrics' => array(
      '#title' => t('Lyrics'),
      '#type' => 'textarea',
      '#rows' => 20,
      '#description' => t('Enter the lyrics here.'),
      '#default_value' => $lyrics,
    ),
    'object' => array(
      '#type' => 'value',
      '#value' => $object->id,
    ),
    'submit' => array(
      '#type' => 'submit',
      '#value' => t('Save'),
    ),
  );
}

function islandora_album_edit_lyrics_form_submit($form, &$form_state) {
  $object = islandora_object_load($form_state['values']['object']);
  if (!$object['LYRICS']) {
    $lyrics = $object->constructDatastream('LYRICS', 'M');
    $lyrics->mimetype = 'text/plain';
    $lyrics->label = 'Audio Lyrics';
  }
  else {
    $lyrics = $object['LYRICS'];
  }
  $lyrics->setContentFromString($form_state['values']['lyrics']);
  $object->ingestDatastream($lyrics);
  drupal_set_message(t("Lyrics updated for !object.", array('!object' => $object->id)));
  drupal_goto("islandora/object/{$object->id}/manage/datastreams");
}
