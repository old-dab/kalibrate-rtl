/*
 * Copyright (c) 2010, Joshua Lackey
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     *  Redistributions of source code must retain the above copyright
 *        notice, this list of conditions and the following disclaimer.
 *
 *     *  Redistributions in binary form must reproduce the above copyright
 *        notice, this list of conditions and the following disclaimer in the
 *        documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "usrp_source.h"
#include "circular_buffer.h"
#include "fcch_detector.h"
#include "arfcn_freq.h"
#include "util.h"

extern int g_verbosity;

static const float ERROR_DETECT_OFFSET_MAX = 40e3;

#ifdef _WIN32
#define BUFSIZ 1024
#endif

static double vectornorm2(const complex *v, const unsigned int len)
{
	unsigned int i;
	double e = 0.0;

	for(i = 0; i < len; i++)
		e += norm(v[i]);

	return e;
}


int c0_detect(usrp_source *u, int bi)
{
	int i, tuner_gain;
	unsigned int overruns, b_len, frames_len, found_count, r;
	float offset, effective_offset, min_offset, max_offset, snr = 0.0f;
	double freq, sps, power;
	complex *b;
	circular_buffer *ub;
	fcch_detector *detector = new fcch_detector(u->sample_rate());

	if(bi == BI_NOT_DEFINED)
	{
		fprintf(stderr, "error: c0_detect: band not defined\n");
		return -1;
	}

	sps = u->sample_rate() / GSM_RATE;
	frames_len = (unsigned int)ceil((12 * 8 * 156.25 + 156.25) * sps);
	ub = u->get_buffer();

	u->start();
	u->flush();
	found_count = 0;
	for(i = first_chan(bi); i >= 0; i = next_chan(i, bi))
	{
		freq = arfcn_to_freq(i, &bi);
		if(!u->tune(freq))
		{
			fprintf(stderr, "error: usrp_source::tune\n");
			return -1;
		}
		if (isatty(1) && g_verbosity == 0)
		{
			printf("...chan %4i\r", i);
			fflush(stdout);
		}
		usleep(50000);
		do
		{
			u->flush();
			if(u->fill(frames_len, &overruns))
			{
				fprintf(stderr, "error: usrp_source::fill\n");
				return -1;
			}
		} while(overruns);

		// first, we calculate the power in each channel
		b = (complex *)ub->peek(&b_len);
		power = sqrt(vectornorm2(b, frames_len) / frames_len);

		r = detector->scan(b, b_len, &offset, 0, &snr);
		effective_offset = offset - GSM_RATE / 4;
		tuner_gain = u->get_tuner_gain();
		if(r && (fabsf(effective_offset) < ERROR_DETECT_OFFSET_MAX))
		{
			// found
			if (found_count)
			{
				min_offset = fmin(min_offset, effective_offset);
				max_offset = fmax(max_offset, effective_offset);
			}
			else
			{
				min_offset = max_offset = effective_offset;
			}
			found_count++;
			printf("    chan: %4d (%.1fMHz ", i, freq / 1e6);
			display_freq(effective_offset);
			printf(")    power: %5.0f \ttuner gain: %ddB \tsnr: %.0f\n", power, tuner_gain, snr);
		}
		else if(g_verbosity > 0)
		{
			printf("    chan: %4d (%.1fMHz):\tpower: %5.0f \ttuner gain: %ddB \tsnr: %.0f\n",
			   i, freq / 1e6, power, tuner_gain, snr);
		}

	}
	printf("%d base stations found !\n", found_count);

	if (found_count == 1)
	{
		printf("\n");
		printf("Only one channel was found. This is unlikely and may "
			"indicate you need to provide a rough estimate of the initial "
			"PPM. It can be provided with the '-e' option. Try tuning against "
			"a local FM radio or other known frequency first.\n");
	}

	/*
	 * If the difference in offsets found is strangely large
	 */
	if (found_count > 1 && max_offset - min_offset > 1000)
	{
		printf("\n");
		printf("Difference of offsets between channels is >1kHz. This likely "
			"means that the correct PPM is too far away and you need to provide "
			"a rough estimate using the '-e' option. Try tuning against "
			"a local FM radio or other known frequency first.\n");
	}
	return 0;
}
