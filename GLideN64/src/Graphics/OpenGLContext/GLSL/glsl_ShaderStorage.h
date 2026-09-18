#pragma once
#include <Graphics/OpenGLContext/opengl_GLInfo.h>

namespace opengl {
	class CachedUseProgram;
}

namespace glsl {

	class ShaderStorage
	{
	public:
		/* Sanity bounds for values read out of the on-disk shader cache. They
		 * are generous -- the point is to reject corruption before it becomes
		 * a huge allocation or an unbounded loop, not to constrain real data. */
		static const size_t maxShaderBinarySize = 4u * 1024u * 1024u;
		static const size_t maxCombinerEntries  = 1u << 20;
		static const size_t maxVersionStringLen = 4096u;

		ShaderStorage(const opengl::GLInfo & _glinfo, opengl::CachedUseProgram * _useProgram);

		bool saveShadersStorage(const graphics::Combiners & _combiners) const;

		bool loadShadersStorage(graphics::Combiners & _combiners);

	private:
		bool _saveCombinerKeys(const graphics::Combiners & _combiners) const;
		bool _loadFromCombinerKeys(graphics::Combiners & _combiners);

		const u32 m_formatVersion = 0x3BU;
		const u32 m_keysFormatVersion = 0x05;
		const opengl::GLInfo & m_glinfo;
		opengl::CachedUseProgram * m_useProgram;
	};

}
