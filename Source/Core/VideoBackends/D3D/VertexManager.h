// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#pragma once

#include "VideoBackends/D3D/LineAndPointGeometryShader.h"
#include "VideoCommon/VertexManagerBase.h"

namespace DX11
{

class VertexManager : public ::VertexManager
{
public:
	VertexManager();
	~VertexManager();

	NativeVertexFormat* CreateNativeVertexFormat() override;
	void CreateDeviceObjects() override;
	void DestroyDeviceObjects() override;

protected:
	virtual void ResetBuffer(u32 stride) override;
	u16* GetIndexBuffer() { return &LocalIBuffer[0]; }

private:

	void PrepareDrawBuffers(u32 stride);
	void Draw(u32 stride);
	// temp
	void vFlush(bool useDstAlpha) override;

	
	u32 m_vertexDrawOffset{};
	u32 m_indexDrawOffset{};
	
	using PID3D11Buffer = ID3D11Buffer*;

	static const UINT MAX_BUFFER_COUNT = 1;

	u32 m_bufferCursor;
	u32 m_currentBuffer;
	std::array<PID3D11Buffer,MAX_BUFFER_COUNT> m_buffers;

	LineAndPointGeometryShader m_lineAndPointShader;

	std::vector<u8> LocalVBuffer;
	std::vector<u16> LocalIBuffer;
};

}  // namespace
