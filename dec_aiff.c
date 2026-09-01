/*
 *			GPAC - Multimedia Framework C SDK
 *
 *  This file is part of GPAC / AIFF audio decoder filter
 *  based on libaiff (https://sourceforge.net/projects/libaiff/)
 *
 */

#include <gpac/filters.h>
#include <string.h>
#include <stdio.h>

#include <libaiff/libaiff.h>

typedef struct
{
	GF_FilterPid *ipid, *opid;

	Bool is_playing;
	u32 src_timescale;
	u32 codec_id;
	u32 nb_calls;
} GF_AIFFDecCtx;

static GF_Err aiffdec_configure_pid(GF_Filter *filter, GF_FilterPid *pid, Bool is_remove)
{
	const GF_PropertyValue *prop;
	GF_AIFFDecCtx *ctx = (GF_AIFFDecCtx *)gf_filter_get_udta(filter);

	if (is_remove)
	{
		if (ctx->opid)
		{
			gf_filter_pid_remove(ctx->opid);
			ctx->opid = NULL;
		}
		ctx->ipid = NULL;
		return GF_OK;
	}
	if (!gf_filter_pid_check_caps(pid))
		return GF_NOT_SUPPORTED;

	prop = gf_filter_pid_get_property(pid, GF_PROP_PID_CODECID);
	if (!prop)
		return GF_NOT_SUPPORTED;
	ctx->ipid = pid;

	if (!ctx->opid)
	{
		ctx->opid = gf_filter_pid_new(filter);
	}

	gf_filter_pid_copy_properties(ctx->opid, ctx->ipid);
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_CODECID, &PROP_UINT(GF_CODECID_RAW));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_AUDIO_FORMAT, &PROP_UINT(GF_AUDIO_FMT_S16));

	return GF_OK;
}

static GF_Err aiffdec_process(GF_Filter *filter)
{
	GF_FilterPacket *pck, *dst_pck;
	u8 *data, *output;
	u32 size, out_size;
	char path[64];
	FILE *fp;
	AIFF_Ref ref;
	uint64_t nSamples = 0;
	int channels = 0, bitsPerSample = 0, segmentSize = 0;
	double samplingRate = 0;
	float *fbuf;
	s16 *out16;
	u32 total_samples, i, n_read;
	GF_AIFFDecCtx *ctx = (GF_AIFFDecCtx *)gf_filter_get_udta(filter);

	pck = gf_filter_pid_get_packet(ctx->ipid);
	if (!pck)
	{
		if (gf_filter_pid_is_eos(ctx->ipid))
		{
			gf_filter_pid_set_eos(ctx->opid);
			return GF_EOS;
		}
		return GF_OK;
	}
	data = (u8 *)gf_filter_pck_get_data(pck, &size);

	if (!data)
	{
		gf_filter_pid_drop_packet(ctx->ipid);
		return GF_IO_ERR;
	}

	snprintf(path, sizeof(path), "/tmp/gpac_aiffdec_%u.aiff", ctx->nb_calls++);
	fp = fopen(path, "wb");
	if (!fp)
	{
		gf_filter_pid_drop_packet(ctx->ipid);
		return GF_IO_ERR;
	}
	fwrite(data, 1, size, fp);
	fclose(fp);

	ref = AIFF_OpenFile(path, F_RDONLY);
	if (!ref)
	{
		remove(path);
		gf_filter_pid_drop_packet(ctx->ipid);
		return GF_NON_COMPLIANT_BITSTREAM;
	}

	if (AIFF_GetAudioFormat(ref, &nSamples, &channels, &samplingRate, &bitsPerSample, &segmentSize) < 1 ||
	    !nSamples || !channels || !samplingRate)
	{
		AIFF_Close(ref);
		remove(path);
		gf_filter_pid_drop_packet(ctx->ipid);
		return GF_NON_COMPLIANT_BITSTREAM;
	}

	total_samples = (u32)(nSamples * channels);
	fbuf = (float *)gf_malloc(sizeof(float) * total_samples);
	if (!fbuf)
	{
		AIFF_Close(ref);
		remove(path);
		gf_filter_pid_drop_packet(ctx->ipid);
		return GF_OUT_OF_MEM;
	}

	n_read = (u32)AIFF_ReadSamplesFloat(ref, fbuf, (int)total_samples);
	AIFF_Close(ref);
	remove(path);

	if ((s32)n_read <= 0)
	{
		gf_free(fbuf);
		gf_filter_pid_drop_packet(ctx->ipid);
		return GF_NON_COMPLIANT_BITSTREAM;
	}

	out_size = n_read * sizeof(s16);
	dst_pck = gf_filter_pck_new_alloc(ctx->opid, out_size, &output);
	if (!dst_pck)
	{
		gf_free(fbuf);
		gf_filter_pid_drop_packet(ctx->ipid);
		return GF_OUT_OF_MEM;
	}

	out16 = (s16 *)output;
	for (i = 0; i < n_read; i++)
	{
		float v = fbuf[i] * 32768.0f;
		if (v > 32767.0f) v = 32767.0f;
		if (v < -32768.0f) v = -32768.0f;
		out16[i] = (s16)v;
	}
	gf_free(fbuf);

	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_SAMPLE_RATE, &PROP_UINT((u32)samplingRate));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_NUM_CHANNELS, &PROP_UINT((u32)channels));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_CHANNEL_LAYOUT, &PROP_LONGUINT((channels == 1) ? GF_AUDIO_CH_FRONT_CENTER : GF_AUDIO_CH_FRONT_LEFT | GF_AUDIO_CH_FRONT_RIGHT));

	gf_filter_pck_merge_properties(pck, dst_pck);
	gf_filter_pck_set_dependency_flags(dst_pck, 0);
	gf_filter_pck_send(dst_pck);

	gf_filter_pid_drop_packet(ctx->ipid);
	return GF_EOS;
}

static const GF_FilterCapability AIFFDecCaps[] =
	{
		CAP_UINT(GF_CAPS_INPUT, GF_PROP_PID_STREAM_TYPE, GF_STREAM_AUDIO),
		CAP_UINT(GF_CAPS_INPUT, GF_PROP_PID_CODECID, GF_4CC('A', 'I', 'F', 'F')),
		CAP_BOOL(GF_CAPS_INPUT_EXCLUDED, GF_PROP_PID_UNFRAMED, GF_TRUE),
		CAP_UINT(GF_CAPS_OUTPUT, GF_PROP_PID_STREAM_TYPE, GF_STREAM_AUDIO),
		CAP_UINT(GF_CAPS_OUTPUT, GF_PROP_PID_CODECID, GF_CODECID_RAW),
};

GF_FilterRegister AIFFDecoderRegister = {
	.name = "aiffdec",
	GF_FS_SET_DESCRIPTION("AIFF decoder")
		GF_FS_SET_HELP("This filter decodes AIFF/AIFC audio using libaiff.")
			.private_size = sizeof(GF_AIFFDecCtx),
	SETCAPS(AIFFDecCaps),
	.configure_pid = aiffdec_configure_pid,
	.process = aiffdec_process,
};

const GF_FilterRegister * EMSCRIPTEN_KEEPALIVE aiffdec_register(GF_FilterSession *session)
{
	return &AIFFDecoderRegister;
}

#include "filter_register.h"
__attribute__((constructor))
void register_aiffdec(void) {
    gf_filter_auto_register("aiffdec", aiffdec_register);
}
