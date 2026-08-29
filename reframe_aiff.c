/*
 *			GPAC - Multimedia Framework C SDK
 *
 *  This file is part of GPAC / AIFF audio reframer filter
 *  based on libaiff (https://sourceforge.net/projects/libaiff/)
 *
 */

#include <gpac/filters.h>
#include <string.h>

typedef struct
{
	GF_FilterPid *ipid;
	GF_FilterPid *opid;
	u32 src_timescale;
	Bool owns_timescale;
	u32 codec_id;

	Bool initial_play_done;
	Bool is_playing;
} GF_ReframeAiffCtx;

static GF_Err rfaiff_configure_pid(GF_Filter *filter, GF_FilterPid *pid, Bool is_remove)
{
	GF_ReframeAiffCtx *ctx = gf_filter_get_udta(filter);
	const GF_PropertyValue *p;

	if (is_remove)
	{
		ctx->ipid = NULL;
		return GF_OK;
	}

	if (!gf_filter_pid_check_caps(pid))
		return GF_NOT_SUPPORTED;

	gf_filter_pid_set_framing_mode(pid, GF_TRUE);
	ctx->ipid = pid;
	ctx->codec_id = 0;

	p = gf_filter_pid_get_property(pid, GF_PROP_PID_TIMESCALE);
	if (p)
		ctx->src_timescale = p->value.uint;

	if (ctx->src_timescale && !ctx->opid)
	{
		ctx->opid = gf_filter_pid_new(filter);
		gf_filter_pid_copy_properties(ctx->opid, ctx->ipid);
		gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_UNFRAMED, NULL);
	}
	ctx->is_playing = GF_TRUE;
	return GF_OK;
}

static Bool rfaiff_process_event(GF_Filter *filter, const GF_FilterEvent *evt)
{
	GF_FilterEvent fevt;
	GF_ReframeAiffCtx *ctx = gf_filter_get_udta(filter);
	if (evt->base.on_pid != ctx->opid)
		return GF_TRUE;
	switch (evt->base.type)
	{
	case GF_FEVT_PLAY:
		if (ctx->is_playing)
			return GF_TRUE;
		ctx->is_playing = GF_TRUE;
		if (!ctx->initial_play_done)
		{
			ctx->initial_play_done = GF_TRUE;
			return GF_TRUE;
		}
		GF_FEVT_INIT(fevt, GF_FEVT_SOURCE_SEEK, ctx->ipid);
		fevt.seek.start_offset = 0;
		gf_filter_pid_send_event(ctx->ipid, &fevt);
		return GF_TRUE;
	case GF_FEVT_STOP:
		ctx->is_playing = GF_FALSE;
		return GF_FALSE;
	default:
		break;
	}
	return GF_TRUE;
}

static GF_Err rfaiff_process(GF_Filter *filter)
{
	GF_ReframeAiffCtx *ctx = gf_filter_get_udta(filter);
	GF_FilterPacket *pck, *dst_pck;
	GF_Err e;
	u8 *data;
	u32 size;

	pck = gf_filter_pid_get_packet(ctx->ipid);
	if (!pck)
	{
		if (gf_filter_pid_is_eos(ctx->ipid))
		{
			if (ctx->opid)
				gf_filter_pid_set_eos(ctx->opid);
			ctx->is_playing = GF_FALSE;
			return GF_EOS;
		}
		return GF_OK;
	}
	data = (u8 *)gf_filter_pck_get_data(pck, &size);

	if (!ctx->opid || !ctx->codec_id)
	{
		if (size < 12 || memcmp(data, "FORM", 4) ||
		    (memcmp(data + 8, "AIFF", 4) && memcmp(data + 8, "AIFC", 4)))
		{
			gf_filter_pid_drop_packet(ctx->ipid);
			return GF_NON_COMPLIANT_BITSTREAM;
		}

		ctx->codec_id = GF_4CC('A', 'I', 'F', 'F');
		ctx->opid = gf_filter_pid_new(filter);
		if (!ctx->opid)
		{
			gf_filter_pid_drop_packet(ctx->ipid);
			return GF_SERVICE_ERROR;
		}

		gf_filter_pid_copy_properties(ctx->opid, ctx->ipid);
		gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_STREAM_TYPE, &PROP_UINT(GF_STREAM_AUDIO));
		gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_CODECID, &PROP_UINT(ctx->codec_id));

		if (!gf_filter_pid_get_property(ctx->ipid, GF_PROP_PID_TIMESCALE))
		{
			gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_TIMESCALE, &PROP_UINT(1000));
			ctx->owns_timescale = GF_TRUE;
		}

		gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_NB_FRAMES, &PROP_UINT(1));
		gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_PLAYBACK_MODE, &PROP_UINT(GF_PLAYBACK_MODE_FASTFORWARD));
	}

	e = GF_OK;

	dst_pck = gf_filter_pck_new_ref(ctx->opid, 0, size, pck);
	if (!dst_pck)
		return GF_OUT_OF_MEM;

	gf_filter_pck_merge_properties(pck, dst_pck);
	if (ctx->owns_timescale)
	{
		gf_filter_pck_set_cts(dst_pck, 0);
		gf_filter_pck_set_sap(dst_pck, GF_FILTER_SAP_1);
		gf_filter_pck_set_duration(dst_pck, 1000);
	}

	gf_filter_pck_send(dst_pck);
	gf_filter_pid_drop_packet(ctx->ipid);

	return e;
}

static const char *rfaiff_probe_data(const u8 *data, u32 size, GF_FilterProbeScore *score)
{
	if (size < 12)
		return NULL;
	if (!memcmp(data, "FORM", 4) && (!memcmp(data + 8, "AIFF", 4) || !memcmp(data + 8, "AIFC", 4)))
	{
		*score = GF_FPROBE_SUPPORTED;
		return "audio/aiff";
	}
	return NULL;
}

static const GF_FilterCapability ReframeAiffCaps[] =
	{
		CAP_UINT(GF_CAPS_INPUT, GF_PROP_PID_STREAM_TYPE, GF_STREAM_FILE),
		CAP_STRING(GF_CAPS_INPUT, GF_PROP_PID_FILE_EXT, "aif|aiff|aifc"),
		CAP_STRING(GF_CAPS_INPUT, GF_PROP_PID_MIME, "audio/aiff|audio/x-aiff"),
		CAP_UINT(GF_CAPS_OUTPUT, GF_PROP_PID_STREAM_TYPE, GF_STREAM_AUDIO),
		CAP_UINT(GF_CAPS_OUTPUT, GF_PROP_PID_CODECID, GF_4CC('A', 'I', 'F', 'F')),
};

GF_FilterRegister ReframeAiffRegister = {
	.name = "rfaiff",
	GF_FS_SET_DESCRIPTION("AIFF audio reframer")
		GF_FS_SET_HELP("This filter parses AIFF/AIFC audio files/data (via libaiff) and outputs corresponding audio PID and frames.\n")
			.private_size = sizeof(GF_ReframeAiffCtx),
	SETCAPS(ReframeAiffCaps),
	.configure_pid = rfaiff_configure_pid,
	.probe_data = rfaiff_probe_data,
	.process = rfaiff_process,
	.process_event = rfaiff_process_event};

const GF_FilterRegister * EMSCRIPTEN_KEEPALIVE dynCall_aiff_reframe_register(GF_FilterSession *session)
{
	return &ReframeAiffRegister;
}

#include "filter_register.h"
__attribute__((constructor))
void register_aiff_reframe(void) {
    gf_filter_auto_register("aiff_reframe", dynCall_aiff_reframe_register);
}
