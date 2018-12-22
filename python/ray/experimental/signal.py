from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

import hashlib
import time

import ray

SYSTEM_ID_STRING = ray.ray_constants.ID_SIZE * b"\xaa"
SYSTEM_ID = ray.ObjectID(SYSTEM_ID_STRING)

SIG_ERROR = 1
SIG_DONE = 2
SIG_USER = 100

START_SIGNAL_COUNTER = 10000

class Signal(object):
    """Signal object"""
    def __init__(self, type, value):
        self.sig_type = type
        self.sig_value = value
    def value(self):
        return self.sig_value
    def type(self):
        return self.sig_type


def _get_signal_id(source_id, counter):
    if  type(source_id) is ray.actor.ActorHandle:
        return ray.raylet.compute_signal_id(
            ray.raylet.compute_task_id(source_id._ray_actor_creation_dummy_object_id),
            counter)
    else:
        return ray.raylet.compute_signal_id(ray.raylet.compute_task_id(source_id), counter)

def task_id(object_id):
    return ray.raylet.compute_task_id(object_id)

def send(signal, source_id = None):
    """Send signal on behalf of source_id.
    Each signal is identified by (source_id, index), where index is incremented
    every time a signal is sent, starting from 1. Receiving this signal,
    requires waiting on (source_id, index).
    Args:
        signal: signal to be sent.
        source_id: If empty, initialize to the id of the task/actor
                   invoking this function.
    """
    if source_id == None:
        if hasattr(ray.worker.global_worker, "actor_creation_task_id"):
            source_key = ray.worker.global_worker.actor_creation_task_id.id()
        else:
            # no actors; this function must have been called from a task
            source_key = ray.worker.global_worker.current_task_id.id()
    else:
        source_key = source_id.id()

    index = ray.worker.global_worker.redis_client.incr(source_key)
    if index < START_SIGNAL_COUNTER:
        ray.worker.global_worker.redis_client.set(source_key, START_SIGNAL_COUNTER)
        index = START_SIGNAL_COUNTER

    object_id = _get_signal_id(ray.ObjectID(source_key), index)
    ray.worker.global_worker.store_and_register(object_id, signal)

def receive(source_ids, timeout=float('inf')):
    """Get all signals from each source in source_ids.
    For each source_id in source_ids, this function returns all signals
    generated by (or on behalf of) source_id since the last receive() or
    forget() were invoked on source_id. If this is the first call on
    source_id, this function returns all signals generated by (or on
    behalf of) source_id so far.
    Args:
        source_ids: list of source ids whose signals are returned.
        timeout: time it receives for new signals to be generated. If none,
                 return when timeout experies. Measured in seconds.
    Returns:
        The list of signals generated for each source in source_ids. They
        are returned as a list of pairs (source_id, signal). There can be
        more than a signal for the same source_id.
    """
    if not hasattr(ray.worker.global_worker, "signal_counters"):
        ray.worker.global_worker.signal_counters = dict()

    signal_counters = ray.worker.global_worker.signal_counters
    results = []
    previous_time = time.time()
    remaining_time = timeout

    # If we never received a signal from a source_id, initialize the
    # signal couunter for source_id to START_SIGNAL_COUNTER.
    for source_id in source_ids:
        if not source_id in signal_counters:
            signal_counters[source_id] = START_SIGNAL_COUNTER

    # For each source_id compute the id of the next unread signal and store these
    # signals in signal_ids. Also, store the reverse mapping from signals to
    # source ids in the source_id_from_signal_id dictionary.
    source_id_from_signal_id = dict()
    signal_ids = []
    for source_id in source_ids:
        signal_id = _get_signal_id(source_id, signal_counters[source_id])
        signal_ids.append(signal_id)
        source_id_from_signal_id[signal_id] = source_id

    while True:
        ready_ids, _ = ray.wait(signal_ids, num_returns=len(signal_ids), timeout=0)
        if len(ready_ids) > 0:
            for signal_id in ready_ids:
                signal = ray.get(signal_id)
                source_id = source_id_from_signal_id[signal_id]
                if isinstance(signal, Signal):
                    results.append((source_id, signal))
                    if signal.type() == SIG_DONE:
                        del signal_counters[source_id]

                # We read this signal so forget it.
                signal_ids.remove(signal_id)
                del source_id_from_signal_id[signal_id]

                if source_id in signal_counters:
                    # Compute id of the next expected signal for this source id.
                    signal_counters[source_id] += 1
                    signal_id = _get_signal_id(source_id, signal_counters[source_id])
                    signal_ids.append(signal_id)
                    source_id_from_signal_id[signal_id] = source_id
                else:
                    break
            current_time = time.time()
            remaining_time -= (current_time - previous_time)
            previous_time = current_time
            if remaining_time < 0:
                break
        else:
            break


    if (remaining_time < 0) or (len(results) > 0):
        return results

    # No past signals and timeout din't expire. Wait for fure signals
    # or until timeout expires.
    ready_ids, _ = ray.wait(signal_ids, 1, timeout=remaining_time)

    for ready_id in ready_ids:
        signal_counters[source_id_from_signal_id[ready_id]] += 1
        signal = ray.get(signal_id)
        if isinstance(signal, Signal):
            results.append((source_id, signal))
            if signal.type() == SIG_DONE:
                del signal_counters[source_id]

    return results

def forget(source_ids):
    """Ignore all previous signals of each source_id in source_ids.
    The index of the next expected signal from source_id is set to the
    last signal's index plus 1. This means that the next receive() on source_id
    will only get the signals generated by (or on behalf to) source_id after
    this function was invoked.
    Args:
        source_ids: list of source ids whose past signals are forgotten.
    """
    if not hasattr(ray.worker.global_worker, "signal_counters"):
        ray.worker.global_worker.signal_counters = dict()
    signal_counters = ray.worker.global_worker.signal_counters

    for source_id in source_ids:
        source_key = ray.raylet.compute_task_id(source_id._ray_actor_creation_dummy_object_id).id()
        value = ray.worker.global_worker.redis_client.get(source_key)
        if value != None:
            signal_counters[source_id] = int(value) + 1
        else:
            signal_counters[source_id] = START_SIGNAL_COUNTER

def reset():
    """
    Reset the worker state associated with any signals that this worker
    has received so far.
    If the worker calls receive() on a source_id next, it will get all the
    signals generated by (or on behalf of) source_id from the beginning.
    """
    ray.worker.global_worker.signal_counters = dict()
