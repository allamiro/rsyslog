.. _param-omkafka-resubmitonfailure:
.. _omkafka.parameter.module.resubmitonfailure:

resubmitOnFailure
=================

.. index::
   single: omkafka; resubmitOnFailure
   single: resubmitOnFailure

.. summary-start

Retry failed messages when Kafka becomes available.

.. summary-end

This parameter applies to :doc:`../../configuration/modules/omkafka`.

:Name: resubmitOnFailure
:Scope: action
:Type: boolean
:Default: action=off
:Required?: no
:Introduced: 8.28.0

Description
-----------

.. versionadded:: 8.28.0

If enabled, failed messages will be resubmitted automatically when Kafka can
send messages again. Messages rejected because they exceed the maximum size
are dropped.

Action usage
------------

.. _param-omkafka-action-resubmitonfailure:
.. _omkafka.parameter.action.resubmitonfailure:
.. code-block:: rsyslog

   action(type="omkafka" resubmitOnFailure="on")

Notes
-----

- Originally documented as "binary"; uses boolean values.
- When ``resubmitOnFailure="on"`` is active, a broker failure during
  delivery can cause the same message to be sent more than once.
  This happens because two independent retry paths exist: rsyslog's
  own action queue (which retains the message when the action returns
  suspended) and omkafka's internal failed-message queue (populated
  by the librdkafka delivery callback). When the broker recovers,
  both paths attempt to send the message.
  To prevent duplicates, enable idempotent delivery on the Kafka
  producer side via ``confParam``:

  .. code-block:: rsyslog

     action(type="omkafka"
            resubmitOnFailure="on"
            confParam=["enable.idempotence=true", "acks=all"])

  With ``enable.idempotence=true``, the broker uses a producer
  sequence number to detect and discard retried duplicates,
  regardless of which retry path delivers the message.

See also
--------

See also :doc:`../../configuration/modules/omkafka`.

