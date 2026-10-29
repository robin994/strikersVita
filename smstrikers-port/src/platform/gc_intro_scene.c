// Real-time GXM renderer for the gcintro GameCube boot animation, drawn in vitaGL's splash context.
// Adapted from Rinnegatamante's vitaGL custom_splashscreen sample; the scene and its textures come
// from app0:gcintro/ (assets/vita/gcintro in the source tree).
#if defined(PORT_VITA) && defined(STRIKERS_VITA_VITAGL)
#include <vitasdk.h>
#include <vitaGL.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "gc_intro_scene.h"

#define GC_SCENE_MAGIC 0x33494347u
#define GC_MAX_NODES 128
#define GC_MAX_DRAWS 128
#define GC_TEXTURES 5
#define GC_SHADERS 4
#define GC_MSAA SCE_GXM_MULTISAMPLE_NONE // Strikers creates its display without MSAA

enum {
	GC_FLAT = 0,
	GC_TEXTURE = 1,
	GC_MATCAP = 2,
	GC_ENVIRONMENT = 3
};

typedef struct {
	uint32_t magic, version, node_count, part_count, track_count, key_count;
	uint32_t vertex_count, index_count, camera_node;
	float duration, camera_xmag, camera_ymag;
	uint32_t offsets[6];
	uint32_t mesh_count, texture_count;
} GcHeader;

typedef struct {
	int32_t parent, mesh, opacity_node;
	float opacity_factor;
	float position[3], rotation[4], scale[3];
} GcNode;

typedef struct {
	uint32_t mesh, mode;
	int32_t texture;
	uint32_t first_vertex, first_index, index_count;
	float color[4];
	uint32_t reserved[2];
} GcPart;

typedef struct {
	uint32_t node, path, first_key, key_count;
} GcTrack;

typedef struct {
	float time, value[4];
} GcKey;

typedef struct {
	float position[3], normal[3], uv[2];
} GcVertex;

typedef struct {
	float position[3], rotation[4], scale[3];
} GcPose;

typedef struct {
	void *pixels;
	SceGxmTexture texture;
} GcTexture;

typedef struct {
	void *binary;
	SceGxmShaderPatcherId id;
	int registered;
	SceGxmFragmentProgram *program[2];
	const SceGxmProgramParameter *tint;
} GcFragment;

typedef struct {
	uint16_t node, part;
	int order;
	float depth, opacity;
} GcDraw;

typedef struct {
	void *scene_data;
	uint32_t scene_size;
	const GcHeader *header;
	const GcNode *nodes;
	const GcPart *parts;
	const GcTrack *tracks;
	const GcKey *keys;
	const GcVertex *vertices;
	const uint16_t *indices;
	GcTexture textures[GC_TEXTURES];
	SceGxmShaderPatcher *patcher;
	void *vertex_binary;
	SceGxmShaderPatcherId vertex_id;
	int vertex_registered;
	SceGxmVertexProgram *vertex_program;
	const SceGxmProgramParameter *mvp_param, *mv_param, *camera_world_param;
	GcFragment fragments[GC_SHADERS];
	GcPose pose[GC_MAX_NODES];
	float world[GC_MAX_NODES][16];
	uint8_t evaluated[GC_MAX_NODES];
	GcDraw opaque[GC_MAX_DRAWS], translucent[GC_MAX_DRAWS];
	int opaque_count, translucent_count, ready, draw_error_reported;
} GcScene;

static GcScene scene;

static int valid_section(const GcHeader *h, uint32_t size, unsigned index,
						 uint32_t count, uint32_t stride) {
	uint32_t start = h->offsets[index];
	return start <= size && count <= (size - start) / stride && !(start & 15);
}

static void *read_asset(const char *name, uint32_t *length) {
	SceUID fd = sceIoOpen(name, SCE_O_RDONLY, 0);
	if (fd < 0) {
		sceClibPrintf("[gcintro] Cannot open %s: 0x%08X\n", name, fd);
		return NULL;
	}
	SceOff size = sceIoLseek(fd, 0, SCE_SEEK_END);
	if (size <= 0 || size > 16 * 1024 * 1024 ||
		sceIoLseek(fd, 0, SCE_SEEK_SET) < 0) {
		sceIoClose(fd);
		return NULL;
	}
	void *data = vglAlloc((uint32_t)size, VGL_MEM_RAM);
	if (!data) {
		sceIoClose(fd);
		return NULL;
	}
	uint32_t done = 0;
	while (done < (uint32_t)size) {
		int read = sceIoRead(fd, (uint8_t *)data + done, (uint32_t)size - done);
		if (read <= 0)
			break;
		done += read;
	}
	sceIoClose(fd);
	if (done != (uint32_t)size) {
		vglFree(data);
		return NULL;
	}
	if (length)
		*length = done;
	return data;
}

static int load_texture(unsigned index) {
	char name[96];
	sceClibSnprintf(name, sizeof(name), GC_INTRO_DIR "texture_%u.bin", index);
	uint32_t size;
	void *file = read_asset(name, &size);
	if (!file)
		return -1;
	if (size < 8) {
		vglFree(file);
		return -1;
	}
	uint32_t width = ((uint32_t *)file)[0];
	uint32_t height = ((uint32_t *)file)[1];
	uint32_t face_count = index == 0 ? 6 : 1;
	if (width < 8 || width > 2048 || height < 8 || height > 2048 ||
		(index == 0 && width != height) ||
		(uint64_t)width * height * 4 * face_count != size - 8) {
		vglFree(file);
		return -1;
	}
	// GXM linear textures require an 8-pixel row pitch, including the 84px labels.
	// Cubemap faces are already swizzled and tightly packed by the exporter.
	uint32_t row_pitch = index == 0 ? width : (width + 7u) & ~7u;
	void *pixels = vglAlloc(row_pitch * height * 4 * face_count, VGL_MEM_RAM);
	if (!pixels) {
		vglFree(file);
		return -1;
	}
	if (index == 0 || row_pitch == width) {
		memcpy(pixels, (uint8_t *)file + 8, size - 8);
		// Strikers: three.js takes an env map's colour only; the material opacity alone is the
		// alpha. The big cube's source image has a ragged alpha channel, and multiplying it in
		// (splash_env_f) left the finished logo patchily transparent and dark.
		if (index == 0)
			for (uint32_t i = 3; i < size - 8; i += 4)
				((uint8_t *)pixels)[i] = 255;
	} else {
		memset(pixels, 0, row_pitch * height * 4);
		for (uint32_t y = 0; y < height; y++)
			memcpy((uint8_t *)pixels + y * row_pitch * 4,
				(uint8_t *)file + 8 + y * width * 4, width * 4);
	}
	vglFree(file);
	scene.textures[index].pixels = pixels;
	SceGxmTexture *tex = &scene.textures[index].texture;
	int result = index == 0 ?
		sceGxmTextureInitCube(tex, pixels,
			SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR, width, height, 1) :
		sceGxmTextureInitLinear(tex, pixels,
			SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR, width, height, 1);
	if (result < 0 || (index == 0 &&
		sceGxmTextureGetType(tex) != SCE_GXM_TEXTURE_CUBE))
		return -1;
	result = sceGxmTextureValidate(tex);
	if (result < 0) {
		sceClibPrintf("[gcintro] Invalid texture %u: 0x%08X\n", index, result);
		return -1;
	}
	sceGxmTextureSetMinFilter(tex, SCE_GXM_TEXTURE_FILTER_LINEAR);
	sceGxmTextureSetMagFilter(tex, SCE_GXM_TEXTURE_FILTER_LINEAR);
	sceGxmTextureSetUAddrMode(tex, index == GC_TEXTURES - 1 ?
		SCE_GXM_TEXTURE_ADDR_REPEAT : SCE_GXM_TEXTURE_ADDR_CLAMP);
	sceGxmTextureSetVAddrMode(tex, index == GC_TEXTURES - 1 ?
		SCE_GXM_TEXTURE_ADDR_REPEAT : SCE_GXM_TEXTURE_ADDR_CLAMP);
	return 0;
}

static int load_scene(void) {
	scene.scene_data = read_asset(GC_INTRO_DIR "scene.bin", &scene.scene_size);
	if (!scene.scene_data || scene.scene_size < sizeof(GcHeader))
		return -1;
	const GcHeader *h = scene.scene_data;
	if (h->magic != GC_SCENE_MAGIC || h->version != 1 ||
		h->node_count == 0 || h->node_count > GC_MAX_NODES ||
		h->part_count == 0 || h->part_count > GC_MAX_DRAWS ||
		h->camera_node >= h->node_count || !h->mesh_count ||
		h->texture_count != GC_TEXTURES ||
		!valid_section(h, scene.scene_size, 0, h->node_count, sizeof(GcNode)) ||
		!valid_section(h, scene.scene_size, 1, h->part_count, sizeof(GcPart)) ||
		!valid_section(h, scene.scene_size, 2, h->track_count, sizeof(GcTrack)) ||
		!valid_section(h, scene.scene_size, 3, h->key_count, sizeof(GcKey)) ||
		!valid_section(h, scene.scene_size, 4, h->vertex_count, sizeof(GcVertex)) ||
		!valid_section(h, scene.scene_size, 5, h->index_count, sizeof(uint16_t)))
		return -1;
	scene.header = h;
	scene.nodes = (const GcNode *)((const uint8_t *)h + h->offsets[0]);
	scene.parts = (const GcPart *)((const uint8_t *)h + h->offsets[1]);
	scene.tracks = (const GcTrack *)((const uint8_t *)h + h->offsets[2]);
	scene.keys = (const GcKey *)((const uint8_t *)h + h->offsets[3]);
	scene.vertices = (const GcVertex *)((const uint8_t *)h + h->offsets[4]);
	scene.indices = (const uint16_t *)((const uint8_t *)h + h->offsets[5]);
	for (uint32_t i = 0; i < h->node_count; i++) {
		const GcNode *n = &scene.nodes[i];
		if (n->parent >= (int32_t)h->node_count ||
			n->mesh >= (int32_t)h->mesh_count ||
			n->opacity_node >= (int32_t)h->node_count)
			return -1;
	}
	for (uint32_t i = 0; i < h->part_count; i++) {
		const GcPart *p = &scene.parts[i];
		if (p->mesh >= h->mesh_count || p->mode >= GC_SHADERS ||
			p->first_vertex >= h->vertex_count ||
			p->first_index > h->index_count ||
			p->index_count > h->index_count - p->first_index ||
			p->texture >= GC_TEXTURES)
			return -1;
	}
	for (uint32_t i = 0; i < h->track_count; i++) {
		const GcTrack *t = &scene.tracks[i];
		if (t->node >= h->node_count || t->path > 2 || !t->key_count ||
			t->first_key > h->key_count ||
			t->key_count > h->key_count - t->first_key)
			return -1;
	}
	sceClibPrintf("[gcintro] 3D scene: %u nodes, %u mesh parts, %u tracks, %u keys\n",
		h->node_count, h->part_count, h->track_count, h->key_count);
	return 0;
}

static int register_shaders(void) {
	static const char *const paths[] = {
		GC_INTRO_DIR "splash_f.gxp", GC_INTRO_DIR "splash_texture_f.gxp",
		GC_INTRO_DIR "splash_matcap_f.gxp", GC_INTRO_DIR "splash_env_f.gxp"
	};
	scene.vertex_binary = read_asset(GC_INTRO_DIR "splash_v.gxp", NULL);
	if (!scene.vertex_binary)
		return -1;
	const SceGxmProgram *vertex = scene.vertex_binary;
	if (sceGxmShaderPatcherRegisterProgram(scene.patcher, vertex,
			&scene.vertex_id) < 0)
		return -1;
	scene.vertex_registered = 1;

	const SceGxmProgramParameter *position =
		sceGxmProgramFindParameterByName(vertex, "position");
	const SceGxmProgramParameter *normal =
		sceGxmProgramFindParameterByName(vertex, "normal");
	const SceGxmProgramParameter *uv =
		sceGxmProgramFindParameterByName(vertex, "uv");
	scene.mvp_param = sceGxmProgramFindParameterByName(vertex, "mvp");
	scene.mv_param = sceGxmProgramFindParameterByName(vertex, "mv");
	scene.camera_world_param = sceGxmProgramFindParameterByName(vertex, "camera_world");
	if (!position || !normal || !uv || !scene.mvp_param || !scene.mv_param ||
		!scene.camera_world_param)
		return -1;

	SceGxmVertexAttribute attrs[3] = {
		{ .streamIndex = 0, .offset = 0, .format = SCE_GXM_ATTRIBUTE_FORMAT_F32,
		  .componentCount = 3, .regIndex = sceGxmProgramParameterGetResourceIndex(position) },
		{ .streamIndex = 0, .offset = 12, .format = SCE_GXM_ATTRIBUTE_FORMAT_F32,
		  .componentCount = 3, .regIndex = sceGxmProgramParameterGetResourceIndex(normal) },
		{ .streamIndex = 0, .offset = 24, .format = SCE_GXM_ATTRIBUTE_FORMAT_F32,
		  .componentCount = 2, .regIndex = sceGxmProgramParameterGetResourceIndex(uv) }
	};
	SceGxmVertexStream stream = { .stride = sizeof(GcVertex),
		.indexSource = SCE_GXM_INDEX_SOURCE_INDEX_16BIT };
	if (sceGxmShaderPatcherCreateVertexProgram(scene.patcher, scene.vertex_id,
			attrs, 3, &stream, 1, &scene.vertex_program) < 0)
		return -1;

	SceGxmBlendInfo blend = {
		.colorMask = SCE_GXM_COLOR_MASK_ALL,
		.colorFunc = SCE_GXM_BLEND_FUNC_ADD,
		.alphaFunc = SCE_GXM_BLEND_FUNC_ADD,
		.colorSrc = SCE_GXM_BLEND_FACTOR_SRC_ALPHA,
		.colorDst = SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		.alphaSrc = SCE_GXM_BLEND_FACTOR_ONE,
		.alphaDst = SCE_GXM_BLEND_FACTOR_ZERO
	};
	for (unsigned i = 0; i < GC_SHADERS; i++) {
		GcFragment *shader = &scene.fragments[i];
		shader->binary = read_asset(paths[i], NULL);
		if (!shader->binary)
			return -1;
		const SceGxmProgram *fragment = shader->binary;
		if (sceGxmShaderPatcherRegisterProgram(scene.patcher, fragment,
				&shader->id) < 0)
			return -1;
		shader->registered = 1;
		shader->tint = sceGxmProgramFindParameterByName(fragment, "tint");
		if (!shader->tint)
			return -1;
		for (int j = 0; j < 2; j++) {
			if (sceGxmShaderPatcherCreateFragmentProgram(scene.patcher,
					shader->id, SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,
					GC_MSAA, j ? &blend : NULL, vertex, &shader->program[j]) < 0)
				return -1;
		}
	}
	return 0;
}

static void mat_identity(float *m) {
	memset(m, 0, 16 * sizeof(float));
	m[0] = m[5] = m[10] = m[15] = 1.f;
}

static void mat_mul(float *out, const float *a, const float *b) {
	float tmp[16];
	for (int r = 0; r < 4; r++)
		for (int c = 0; c < 4; c++) {
			float v = 0.f;
			for (int k = 0; k < 4; k++)
				v += a[r * 4 + k] * b[k * 4 + c];
			tmp[r * 4 + c] = v;
		}
	memcpy(out, tmp, sizeof(tmp));
}

static void mat_transpose(float *out, const float *in) {
	for (int r = 0; r < 4; r++)
		for (int c = 0; c < 4; c++)
			out[r * 4 + c] = in[c * 4 + r];
}

static void mat_trs(float *m, const GcPose *p) {
	const float x = p->rotation[0], y = p->rotation[1];
	const float z = p->rotation[2], w = p->rotation[3];
	const float sx = p->scale[0], sy = p->scale[1], sz = p->scale[2];
	m[0] = (1 - 2 * (y*y + z*z)) * sx;
	m[1] = 2 * (x*y - z*w) * sy;
	m[2] = 2 * (x*z + y*w) * sz;
	m[3] = p->position[0];
	m[4] = 2 * (x*y + z*w) * sx;
	m[5] = (1 - 2 * (x*x + z*z)) * sy;
	m[6] = 2 * (y*z - x*w) * sz;
	m[7] = p->position[1];
	m[8] = 2 * (x*z - y*w) * sx;
	m[9] = 2 * (y*z + x*w) * sy;
	m[10] = (1 - 2 * (x*x + y*y)) * sz;
	m[11] = p->position[2];
	m[12] = m[13] = m[14] = 0.f;
	m[15] = 1.f;
}

static void world_matrix(uint32_t i) {
	if (scene.evaluated[i])
		return;
	scene.evaluated[i] = 1;
	float local[16];
	mat_trs(local, &scene.pose[i]);
	int parent = scene.nodes[i].parent;
	if (parent < 0)
		memcpy(scene.world[i], local, sizeof(local));
	else {
		world_matrix((uint32_t)parent);
		mat_mul(scene.world[i], scene.world[parent], local);
	}
}

static void camera_inverse(float *view, const float *camera) {
	mat_identity(view);
	for (int r = 0; r < 3; r++)
		for (int c = 0; c < 3; c++)
			view[r * 4 + c] = camera[c * 4 + r];
	for (int r = 0; r < 3; r++)
		view[r * 4 + 3] = -(view[r * 4] * camera[3] +
			view[r * 4 + 1] * camera[7] + view[r * 4 + 2] * camera[11]);
}

static void quaternion_slerp(float *out, const float *a, const float *b, float blend) {
	float dot = a[0]*b[0] + a[1]*b[1] + a[2]*b[2] + a[3]*b[3];
	float sign = 1.f;
	if (dot < 0.f) {dot = -dot;sign = -1.f;}
	float w0 = 1.f - blend, w1 = blend * sign;
	if (dot < .9995f) {
		float theta = acosf(fminf(dot, 1.f));
		float denom = sinf(theta);
		w0 = sinf((1.f - blend) * theta) / denom;
		w1 = sign * sinf(blend * theta) / denom;
	}
	float len = 0.f;
	for (int i = 0; i < 4; i++) {
		out[i] = w0 * a[i] + w1 * b[i];
		len += out[i] * out[i];
	}
	len = 1.f / sqrtf(len);
	for (int i = 0; i < 4; i++)
		out[i] *= len;
}

static void animate(float time) {
	const GcHeader *h = scene.header;
	time = fminf(fmaxf(time, 0.f), h->duration);
	for (uint32_t i = 0; i < h->node_count; i++) {
		memcpy(scene.pose[i].position, scene.nodes[i].position, 3 * sizeof(float));
		memcpy(scene.pose[i].rotation, scene.nodes[i].rotation, 4 * sizeof(float));
		memcpy(scene.pose[i].scale, scene.nodes[i].scale, 3 * sizeof(float));
	}
	for (uint32_t i = 0; i < h->track_count; i++) {
		const GcTrack *track = &scene.tracks[i];
		const GcKey *first = &scene.keys[track->first_key];
		uint32_t lo = 0, hi = track->key_count - 1;
		while (lo < hi) {
			uint32_t middle = (lo + hi + 1) / 2;
			if (first[middle].time <= time) lo = middle;
			else hi = middle - 1;
		}
		const GcKey *left = &first[lo];
		const GcKey *right = &first[lo + (lo + 1 < track->key_count)];
		float blend = 0.f;
		if (time > left->time && right->time > left->time)
			blend = fminf(1.f, (time - left->time) / (right->time - left->time));
		float *destination = track->path == 0 ? scene.pose[track->node].position :
			track->path == 1 ? scene.pose[track->node].rotation : scene.pose[track->node].scale;
		if (track->path == 1)
			quaternion_slerp(destination, left->value, right->value, blend);
		else for (int c = 0; c < 3; c++)
			destination[c] = left->value[c] + blend * (right->value[c] - left->value[c]);
	}
	memset(scene.evaluated, 0, sizeof(scene.evaluated));
	for (uint32_t i = 0; i < h->node_count; i++)
		world_matrix(i);
}

static void submit_part(const GcDraw *draw, const float *view,
						const float *projection, const float *camera_world_gpu,
						const vglSplashDetails *details, float fade,
						int translucent) {
	const GcPart *part = &scene.parts[draw->part];
	const GcFragment *shader = &scene.fragments[part->mode];
	float mv[16], mvp[16], mv_gpu[16], mvp_gpu[16];
	mat_mul(mv, view, scene.world[draw->node]);
	mat_mul(mvp, projection, mv);
	mat_transpose(mv_gpu, mv);
	mat_transpose(mvp_gpu, mvp);
	SceGxmContext *ctx = details->context;
	// Strikers: three.js r124 state for gcintro's materials. Every one is transparent and blended,
	// and all but the x/y/z letters (GC_TEXTURE) still write depth. Only the big cube, the trails and
	// the letters are double-sided; the moving cube and the NINTENDO/GAMECUBE text cull back faces
	// (CW on vitaGL's display, as glFrontFace(GL_CCW) + glCullFace(GL_BACK) maps there).
	(void)translucent;
	const int double_sided = part->mesh == 0 || part->mesh == 2 || (part->mesh >= 3 && part->mesh <= 5);
	sceGxmSetCullMode(ctx, double_sided ? SCE_GXM_CULL_NONE : SCE_GXM_CULL_CW);
	// The moving cube's polygonOffsetFactor -150 keeps it in front of the big cube it sits in.
	sceGxmSetFrontDepthBias(ctx, part->mesh == 1 ? -150 : 0, 0);
	sceGxmSetFrontDepthFunc(ctx, SCE_GXM_DEPTH_FUNC_LESS_EQUAL);
	sceGxmSetFrontDepthWriteEnable(ctx, part->mode == GC_TEXTURE ?
		SCE_GXM_DEPTH_WRITE_DISABLED : SCE_GXM_DEPTH_WRITE_ENABLED);
	sceGxmSetVertexProgram(ctx, scene.vertex_program);
	sceGxmSetFragmentProgram(ctx, shader->program[1]);
	sceGxmSetVertexStream(ctx, 0, scene.vertices + part->first_vertex);
	if (part->texture >= 0)
		sceGxmSetFragmentTexture(ctx, 0, &scene.textures[part->texture].texture);
	void *vbuffer, *fbuffer;
	sceGxmReserveVertexDefaultUniformBuffer(ctx, &vbuffer);
	sceGxmSetUniformDataF(vbuffer, scene.mvp_param, 0, 16, mvp_gpu);
	sceGxmSetUniformDataF(vbuffer, scene.mv_param, 0, 16, mv_gpu);
	sceGxmSetUniformDataF(vbuffer, scene.camera_world_param, 0, 16, camera_world_gpu);
	float tint[4] = {
		part->color[0] * fade, part->color[1] * fade,
		part->color[2] * fade, draw->opacity * part->color[3]
	};
	sceGxmReserveFragmentDefaultUniformBuffer(ctx, &fbuffer);
	sceGxmSetUniformDataF(fbuffer, shader->tint, 0, 4, tint);
	int result = sceGxmDraw(ctx, SCE_GXM_PRIMITIVE_TRIANGLES,
		SCE_GXM_INDEX_FORMAT_U16, scene.indices + part->first_index,
		part->index_count);
	if (result < 0 && !scene.draw_error_reported) {
		scene.draw_error_reported = 1;
		sceClibPrintf("[gcintro] GXM draw failed: 0x%08X\n", result);
	}
}

int gcSceneInit(const vglSplashDetails *details) {
	memset(&scene, 0, sizeof(scene));
	scene.patcher = details->patcher;
	if (load_scene() < 0)
		goto error;
	for (unsigned i = 0; i < GC_TEXTURES; i++)
		if (load_texture(i) < 0)
			goto error;
	if (register_shaders() < 0)
		goto error;
	scene.ready = 1;
	return 0;
error:
	sceClibPrintf("[gcintro] Failed to initialize live 3D scene\n");
	gcSceneTerm();
	return -1;
}

float gcSceneDuration(void) {
	return scene.header ? scene.header->duration : 0.f;
}

void gcSceneDraw(const vglSplashDetails *details, float fade) {
	if (!scene.ready)
		return;
	const GcHeader *h = scene.header;
	animate(details->elapsed_seconds);

	float view[16], projection[16], camera_world_gpu[16];
	camera_inverse(view, scene.world[h->camera_node]);
	mat_transpose(camera_world_gpu, scene.world[h->camera_node]);
	mat_identity(projection);
	projection[0] = 1.8f / h->camera_xmag * (272.f / 480.f);
	projection[5] = 1.8f / h->camera_ymag;
	projection[10] = -2.f / (1000.f - .001f);
	projection[11] = -(1000.f + .001f) / (1000.f - .001f);

	scene.opaque_count = scene.translucent_count = 0;
	for (uint32_t n = 0; n < h->node_count; n++) {
		const GcNode *node = &scene.nodes[n];
		if (node->mesh < 0)
			continue;
		float opacity = node->opacity_node < 0 ? 1.f :
			scene.pose[node->opacity_node].position[0] * node->opacity_factor;
		opacity = fminf(fmaxf(opacity, 0.f), 1.f);
		if (opacity < .001f)
			continue;
		float center[16];
		mat_mul(center, view, scene.world[n]);
		for (uint32_t p = 0; p < h->part_count; p++) {
			const GcPart *part = &scene.parts[p];
			if (part->mesh != (uint32_t)node->mesh)
				continue;
			// Strikers: one back-to-front list by render order, as three.js sorts transparent objects.
			int translucent = 1;
			int order = node->mesh == 2 ? 1000 : node->mesh == 14 ? 2000 : 0;
			GcDraw *output;
			if (translucent) {
				if (scene.translucent_count >= GC_MAX_DRAWS) continue;
				output = &scene.translucent[scene.translucent_count++];
			} else {
				if (scene.opaque_count >= GC_MAX_DRAWS) continue;
				output = &scene.opaque[scene.opaque_count++];
			}
			*output = (GcDraw){.node = n, .part = p,
				.opacity = opacity, .depth = center[11], .order = order};
		}
	}
	for (int i = 1; i < scene.translucent_count; i++) {
		GcDraw next = scene.translucent[i];
		int j = i - 1;
		while (j >= 0 && (scene.translucent[j].order > next.order ||
			(scene.translucent[j].order == next.order &&
			 scene.translucent[j].depth > next.depth))) {
			scene.translucent[j + 1] = scene.translucent[j];
			j--;
		}
		scene.translucent[j + 1] = next;
	}
	sceGxmSetCullMode(details->context, SCE_GXM_CULL_NONE);
	// The original Three.js canvas is square and centered inside a 480x272 frame.
	sceGxmSetRegionClip(details->context, SCE_GXM_REGION_CLIP_OUTSIDE,
		208, 0, 751, 543);
	for (int i = 0; i < scene.opaque_count; i++)
		submit_part(&scene.opaque[i], view, projection, camera_world_gpu, details, fade, 0);
	for (int i = 0; i < scene.translucent_count; i++)
		submit_part(&scene.translucent[i], view, projection, camera_world_gpu, details, fade, 1);
	sceGxmSetCullMode(details->context, SCE_GXM_CULL_NONE);
	sceGxmSetFrontDepthBias(details->context, 0, 0);
	// Restore the full framebuffer before vitaGL draws the commit hash.
	sceGxmSetRegionClip(details->context, SCE_GXM_REGION_CLIP_OUTSIDE,
		0, 0, 959, 543);
}

void gcSceneTerm(void) {
	for (int i = 0; i < GC_SHADERS; i++) {
		GcFragment *fragment = &scene.fragments[i];
		if (scene.patcher) {
			for (int j = 0; j < 2; j++)
				if (fragment->program[j])
					sceGxmShaderPatcherReleaseFragmentProgram(scene.patcher, fragment->program[j]);
			if (fragment->registered)
				sceGxmShaderPatcherUnregisterProgram(scene.patcher, fragment->id);
		}
		if (fragment->binary)
			vglFree(fragment->binary);
	}
	if (scene.patcher && scene.vertex_program)
		sceGxmShaderPatcherReleaseVertexProgram(scene.patcher, scene.vertex_program);
	if (scene.patcher && scene.vertex_registered)
		sceGxmShaderPatcherUnregisterProgram(scene.patcher, scene.vertex_id);
	if (scene.vertex_binary)
		vglFree(scene.vertex_binary);
	for (int i = 0; i < GC_TEXTURES; i++)
		if (scene.textures[i].pixels)
			vglFree(scene.textures[i].pixels);
	if (scene.scene_data)
		vglFree(scene.scene_data);
	memset(&scene, 0, sizeof(scene));
}
#endif
