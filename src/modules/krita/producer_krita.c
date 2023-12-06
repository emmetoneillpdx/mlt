/*
 * producer_krita.c -- Produces variable-speed audio within a restricted range of frames. Used internally by Krita to drive audio-synced animation playback.
 * Copyright (C) 2022 Eoin O'Neill <eoinoneill1991@gmail.com>
 * Copyright (C) 2022 Emmet O'Neill <emmetoneill.pdx@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <framework/mlt.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <framework/mlt_factory.h>
#include <framework/mlt_frame.h>
#include <framework/mlt_producer.h>
#include <framework/mlt_property.h>
#include <framework/mlt_service.h>

typedef struct
{
    mlt_producer producer_internal;
} private_data;

/** Restricts frame index to within range by modulus wrapping (not clamping).
*/
static int restrict_range(int index, int min, int max)
{
    const int span = max - min;
    return (MAX(index - min, 0) % (span + 1)) + min;
}

static int is_valid_range(const int frame_start, const int frame_end)
{
    const bool NON_NEGATIVE = frame_start >= 0 && frame_end >= 0;
    const bool NON_INVERTED = frame_end > frame_start;

    return NON_NEGATIVE && NON_INVERTED;
}

static int producer_get_audio(mlt_frame frame,
                              void **buffer,
                              mlt_audio_format *format,
                              int *frequency,
                              int *channels,
                              int *samples)
{
    mlt_producer producer = mlt_frame_pop_audio(frame);

    struct mlt_audio_s audio;

    mlt_audio_set_values(&audio, *buffer, *frequency, *format, *samples, *channels);

    int error = mlt_frame_get_audio(frame,
                                    &audio.data,
                                    &audio.format,
                                    &audio.frequency,
                                    &audio.channels,
                                    &audio.samples);

    mlt_properties props = MLT_PRODUCER_PROPERTIES(producer);

    // Scale the frequency to account for the dynamic speed (normalized).
    const double SPEED = mlt_properties_get_double(props, "speed");

    audio.frequency = (double) audio.frequency * fabs(SPEED);
    if (SPEED < 0.0) {
        mlt_audio_reverse(&audio);
    }

    mlt_audio_get_values(&audio, buffer, frequency, format, samples, channels);

    return error;
}

static int producer_get_frame(mlt_producer producer, mlt_frame_ptr frame, int index)
{
    mlt_properties props = MLT_PRODUCER_PROPERTIES(producer);
    const int FRAME_START = mlt_properties_get_int(props, "start_frame");
    const int FRAME_END = mlt_properties_get_int(props, "end_frame");
    const bool IS_RANGE_LIMITED = mlt_properties_get_int(props, "limit_enabled");

    private_data *pdata = (private_data *) producer->child;
    const int POSITION = mlt_producer_position(pdata->producer_internal);

    if (IS_RANGE_LIMITED && is_valid_range(FRAME_START, FRAME_END)) {
        mlt_properties_set_position(MLT_PRODUCER_PROPERTIES(pdata->producer_internal),
                                    "_position",
                                    restrict_range(POSITION, FRAME_START, FRAME_END));
    }

    int retval = mlt_service_get_frame((mlt_service) pdata->producer_internal, frame, index);

    if (!mlt_frame_is_test_audio(*frame)) {
        mlt_frame_push_audio(*frame, producer);
        mlt_frame_push_audio(*frame, producer_get_audio);
    }

    return retval;
}

static int producer_seek(mlt_producer producer, mlt_position position)
{
    private_data *pdata = (private_data *) producer->child;

    int retval = mlt_producer_seek(pdata->producer_internal, position);

    return retval;
}

static void producer_close(mlt_producer producer)
{
    private_data *pdata = (private_data *) producer->child;

    if (pdata) {
        mlt_producer_close(pdata->producer_internal);
        free(pdata);
    }

    producer->close = NULL;
    mlt_producer_close(producer);
    free(producer);
}

/** Constructor for the producer.
*/
mlt_producer producer_krita_init(mlt_profile profile,
                                 mlt_service_type type,
                                 const char *id,
                                 char *arg)
{
    // Create a new producer object
    mlt_producer producer = mlt_producer_new(profile);
    private_data *pdata = (private_data *) calloc(1, sizeof(private_data));

    if (arg && producer && pdata) {
        mlt_properties producer_properties = MLT_PRODUCER_PROPERTIES(producer);

        // Initialize the producer
        mlt_properties_set(producer_properties, "resource", arg);
        producer->child = pdata;
        producer->get_frame = producer_get_frame;
        producer->seek = producer_seek;
        producer->close = (mlt_destructor) producer_close;

        // Get the resource to be passed to the clip producer
        char *resource = arg;

        // Create a producer for the clip using the false profile.
        pdata->producer_internal = mlt_factory_producer(profile, "abnormal", resource);

        if (pdata->producer_internal) {
            mlt_producer_set_speed(pdata->producer_internal, 1.0);
        }
    }

    const bool INVALID_CONTEXT = !producer || !pdata || !pdata->producer_internal;
    if (INVALID_CONTEXT) { // Clean up early...
        if (pdata) {
            mlt_producer_close(pdata->producer_internal);
            free(pdata);
        }

        if (producer) {
            producer->child = NULL;
            producer->close = NULL;
            mlt_producer_close(producer);
            free(producer);
            producer = NULL;
        }
    }

    return producer;
}
