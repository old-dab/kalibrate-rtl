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

#include "usrp_source.h"
#include "fcch_detector.h"
#include "util.h"

static const unsigned int	AVG_COUNT	= 100;
static const unsigned int	AVG_THRESHOLD	= (AVG_COUNT / 10);
static const float		OFFSET_MAX	= 40e3;

extern int g_verbosity;

int offset_detect(usrp_source *u, int hz_adjust, float tuner_error)
{
	unsigned int new_overruns = 0, overruns = 0;
	int notfound = 0;
	unsigned int s_len, b_len, consumed, count;
	float offset = 0.0, min = 0.0, max = 0.0, avg_offset = 0.0,
		stddev = 0.0, sps, snr, snr_sum = 0.0f, offsets[AVG_COUNT];
	double total_ppm;
	complex *cbuf;
	fcch_detector *l;
	circular_buffer *cb;
	int tuner_gain;

	l = new fcch_detector(u->sample_rate());

	/*
	 * We deliberately grab 12 frames and 1 burst.  We are guaranteed to
	 * find at least one FCCH burst in this much data.
	 */
	sps = u->sample_rate() / GSM_RATE;
	s_len = (unsigned int)ceil((12 * 8 * 156.25 + 156.25) * sps);
	cb = u->get_buffer();

	u->start();
	u->flush();
	count = 0;
	while(count < AVG_COUNT)
	{

		// ensure at least s_len contiguous samples are read from usrp
		do
		{
			if(u->fill(s_len, &new_overruns))
				return -1;
			if(new_overruns)
			{
				overruns += new_overruns;
				u->flush();
			}
		} while(new_overruns);

		// get a pointer to the next samples
		cbuf = (complex *)cb->peek(&b_len);

		snr = 0.0f;
		// search the buffer for a pure tone
		if(l->scan(cbuf, b_len, &offset, &consumed, &snr))
		{

			// FCH is a sine wave at GSM_RATE / 4
			offset = offset - GSM_RATE / 4 - tuner_error;

			// sanity check offset
			if(fabs(offset) < OFFSET_MAX)
			{

				offsets[count] = offset;
				snr_sum += snr;
				count += 1;

				if(g_verbosity > 0)
					printf("\toffset %3u: %.0f \tsnr: %0.f\n", count, offset, snr);
			}
		}
		else
			++notfound;

		// consume used samples
		cb->purge(consumed);
	}

	u->stop();
	delete l;

	// construct stats
	sort(offsets, AVG_COUNT);
	avg_offset = avg(offsets + AVG_THRESHOLD, AVG_COUNT - 2 * AVG_THRESHOLD, &stddev);
	min = offsets[AVG_THRESHOLD];
	max = offsets[AVG_COUNT - AVG_THRESHOLD - 1];

	printf("average\t\t[min, max]\t(range, stddev)\n");
	display_freq(avg_offset);
	printf("\t\t[%d, %d]\t(%d, %.2f)\n", (int)round(min), (int)round(max), (int)round(max - min), stddev);
	printf("overruns: %u\n", overruns);
	printf("not found: %u\n", notfound);

	total_ppm = u->m_freq_corr - ((avg_offset + hz_adjust) / u->m_center_freq) * 1000000;

	printf("average absolute error: %.2f ppm\n", total_ppm);
	tuner_gain = u->get_tuner_gain();
	printf("tuner gain: %ddB \tsnr: %.0f\n", tuner_gain, snr_sum/count);
	return 0;
}
