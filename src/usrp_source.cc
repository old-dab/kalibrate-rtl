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
#ifndef _WIN32
#include <unistd.h>
#endif
#include <string.h>
#include <pthread.h>
#define _USE_MATH_DEFINES
#include <math.h>
#include <complex>

#include "usrp_source.h"

static rtlsdr_dev_t	*dev;

#define IQ_BUFSIZE (1024000)
static short ibuf[IQ_BUFSIZE];
static short qbuf[IQ_BUFSIZE];
static int iq_head = 0;
static int iq_tail = 0;

static void rtlsdr_callback(unsigned char *buf, uint32_t len, void *ctx)
{
	//decimation = 6
	for(unsigned int i=0; i<len; i+=12)
	{
		int j;
		int u = 0;
		for(j=0; j<12; j+=2)
			u += buf[i+j];
		ibuf[iq_head] = u*128/3-32609;
		u = 0;
		for(j=1; j<12; j+=2)
			u += buf[i+j];
		qbuf[iq_head] = u*128/3-32609;
		iq_head++;
		if(iq_head == IQ_BUFSIZE)
			iq_head = 0;
		if(iq_head == iq_tail)
		{
			printf("Buffer full\n");
			break;
	 	}
	}
	//printf("len=%d\n",len);
}

static void *dongle_thread_fn(void *arg)
{
	rtlsdr_read_async(dev, rtlsdr_callback, NULL, 0, 48*512);
	return NULL;
}

usrp_source::usrp_source(void)
{
	m_center_freq = 0.0;
	m_sample_rate = 0.0;
	m_cb = new circular_buffer(CB_LEN, sizeof(complex), 0);
	m_freq_corr = 0;

	pthread_mutex_init(&m_u_mutex, 0);
}


usrp_source::~usrp_source()
{
	stop();
	delete m_cb;
	rtlsdr_cancel_async(dev);
	rtlsdr_close(dev);
	pthread_mutex_destroy(&m_u_mutex);
}


void usrp_source::stop()
{
	pthread_mutex_lock(&m_u_mutex);
	pthread_mutex_unlock(&m_u_mutex);
}


void usrp_source::start()
{
	pthread_mutex_lock(&m_u_mutex);
	pthread_mutex_unlock(&m_u_mutex);
}


float usrp_source::sample_rate()
{
	return m_sample_rate;
}


int usrp_source::tune(double freq)
{
	int r = 0;

	pthread_mutex_lock(&m_u_mutex);
	if (freq != m_center_freq)
	{
		r = rtlsdr_set_center_freq(dev, (uint32_t)freq);
		if (r < 0)
			fprintf(stderr, "Tuning to %u Hz failed!\n", (uint32_t)freq);
		else
			m_center_freq = rtlsdr_get_center_freq(dev);
	}
	pthread_mutex_unlock(&m_u_mutex);

	return (r < 0) ? 0 : 1;
}


int usrp_source::set_freq_correction(int ppm)
{
	m_freq_corr = ppm;
	return rtlsdr_set_freq_correction(dev, ppm);
}


int usrp_source::set_bandwidth(int bandwidth)
{
	int r;
	uint32_t applied_bw = 0;

	r = rtlsdr_set_and_get_tuner_bandwidth(dev, bandwidth, &applied_bw, 1 /* =apply_bw */);
	if (r < 0)
		fprintf(stderr, "WARNING: Failed to set bandwidth.\n");
	else if (bandwidth > 0)
	{
		if (applied_bw)
			printf("Bandwidth parameter %u Hz resulted in %u Hz.\n", bandwidth, applied_bw);
		else
			printf("Set bandwidth parameter %u Hz.\n", bandwidth);
	}
	else
		printf("Bandwidth set to automatic resulted in %u Hz.\n", applied_bw);
	return r;
}


bool usrp_source::set_dithering(bool enable)
{
#if HAVE_DITHERING == 1
	return (bool)(!rtlsdr_set_dithering(dev, (int)enable));
#else
	return true;
#endif
}


bool usrp_source::set_gain(int gain)
{
	int r;

	if (gain == 0)
	{
		r = rtlsdr_set_agc_mode(dev, 1);
		r |= rtlsdr_set_tuner_gain_mode(dev, 0);
		if (r != 0)
			fprintf(stderr, "WARNING: Failed to enable automatic gain.\n");
		else
			printf("Tuner gain set to automatic.\n");
	}
	else
	{
		/* Enable manual gain */
		r = rtlsdr_set_tuner_gain_mode(dev, 1);
		if (r < 0)
			fprintf(stderr, "WARNING: Failed to enable manual gain.\n");
		printf("Setting gain: %d dB\n", gain);
		r = rtlsdr_set_tuner_gain(dev, gain*10);
	}
	return (r < 0) ? 0 : 1;
}


int usrp_source::get_tuner_gain(void)
{
	unsigned char reg_values[256];
	int len;
	int tuner_gain = 0;

#if HAVE_GET_TUNER_GAIN == 1
	rtlsdr_get_tuner_i2c_register(dev, reg_values, &len, &tuner_gain);
	tuner_gain = (tuner_gain + 5) / 10;
#endif
	return tuner_gain;
}


/*
 * open() should be called before multiple threads access usrp_source.
 */
int usrp_source::open(unsigned int dev_index)
{
	int i, r, device_count;
	pthread_t dongle_thread;

	m_sample_rate = 1625000.0 / 6.0; //decimation = 6

	device_count = rtlsdr_get_device_count();
	if (!device_count)
	{
		printf("No supported devices found.\n");
		exit(1);
	}
	printf("Found %d device(s):\n", device_count);
	for (i = 0; i < device_count; i++)
		printf("  %d:  %s\n", i, rtlsdr_get_device_name(i));
	printf("\n");

	printf("Using device %d: %s\n",
		dev_index,
		rtlsdr_get_device_name(dev_index));

	r = rtlsdr_open(&dev, dev_index);
	if (r < 0)
	{
		fprintf(stderr, "Failed to open rtlsdr device #%d.\n", dev_index);
		exit(1);
	}

	/* Set the sample rate */
	r = rtlsdr_set_sample_rate(dev, 1625000);
	if (r < 0)
		fprintf(stderr, "WARNING: Failed to set sample rate.\n");

	/* Reset endpoint before we start reading from it (mandatory) */
	r = rtlsdr_reset_buffer(dev);
	if (r < 0)
		fprintf(stderr, "WARNING: Failed to reset buffers.\n");

	pthread_create(&dongle_thread, NULL, dongle_thread_fn, NULL);
	return 0;
}

rtlsdr_dev_t	*usrp_source::dev_handle()
{
	return dev;
}

int usrp_source::fill(unsigned int num_samples, unsigned int *overrun_i)
{
	complex *c;
	unsigned int i, j = 0, space = 0, overruns = 0;

	while((m_cb->data_available() < num_samples) && (m_cb->space_available() > 0))
	{
		j++;
		// read one usb packet from the usrp
		pthread_mutex_lock(&m_u_mutex);
		i = 0;
		while(((iq_head-iq_tail >= 0) && (iq_head-iq_tail < (int)num_samples)) ||
		      ((iq_head-iq_tail < 0) && (IQ_BUFSIZE+iq_head-iq_tail < (int)num_samples) ))
		{
			i++;
			usleep(1000);
		}
		//printf("num_samples=%d, i=%d\n", num_samples, i);

		// write complex<short> input to complex<float> output
		c = (complex *)m_cb->poke(&space);

		// set space to number of complex items to copy
		space = num_samples;

		// write data
		for(i = 0; i<space; i++)
		{
			c[i] = complex(ibuf[iq_tail], qbuf[iq_tail]);
			iq_tail++;
			if(iq_tail == IQ_BUFSIZE)
				iq_tail = 0;
			//printf("%d/%d ", ibuf[i], qbuf[i]);
		}
		pthread_mutex_unlock(&m_u_mutex);
		// update cb
		m_cb->wrote(i);
	}

	// if the cb is full, we left behind data from the usb packet
	if(m_cb->space_available() == 0)
	{
		fprintf(stderr, "warning: local overrun\n");
		overruns++;
	}

	if(overrun_i)
		*overrun_i = overruns;

	return 0;
}


/*
 * Don't hold a lock on this and use the usrp at the same time.
 */
circular_buffer *usrp_source::get_buffer()
{
	return m_cb;
}

#define FLUSH_SIZE		512

int usrp_source::flush(unsigned int flush_count)
{
	m_cb->flush();
	fill(flush_count * FLUSH_SIZE, 0);
	m_cb->flush();

	pthread_mutex_lock(&m_u_mutex);
	iq_head = 0;
	iq_tail = 0;
	pthread_mutex_unlock(&m_u_mutex);

	return 0;
}
