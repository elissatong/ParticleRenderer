#include "pch.h"
#include "ParticleRenderer.h"
#include "Engine\Common\BasicMath.h"
#include <math.h>
#include <DirectXColors.h>
#include "DirectXHelper.h"
#include <DDSTextureLoader.h>
#include "Engine\Common\BasicLoader.h"


#include <Windows.h>
#include <iostream>
#include <sstream>

#define DBOUT( s )            \
{                             \
   std::wostringstream os_;    \
   os_ << s;                   \
   OutputDebugStringW( os_.str().c_str() );  \
}

using namespace DirectX;
using namespace Microsoft::WRL;
using namespace Windows::Foundation;
using namespace Windows::UI::Core;
using namespace LanguageGameWp8DxComponent;

const float Y_OFFSCREEN_VALUE = -1.0f;
const int X_INDEX = 0;
const int Y_INDEX = 0;

float SCALE_VALUES = 0.00875f;


ParticleRenderer::ParticleRenderer(
	Microsoft::WRL::ComPtr<ID3D11Device1> d3dDevice, 
	Microsoft::WRL::ComPtr<ID3D11DeviceContext1> d3dContext,
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTargetView,
	Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depthStencilView) :

	m_loadingComplete(Idle)
	,m_indexCount(0)
	,m_state(Paused)
	,m_commonStates(nullptr)
	,m_d3dDevice(d3dDevice)
	,m_d3dContext(d3dContext)
	,m_renderTargetView(renderTargetView)
	,m_depthStencilView(depthStencilView)
	,m_deletionRequested(false)
	,m_isPartInfiniteLifetime(false)
	,m_enableTextureRotation(false)
{		
	//==================================
	// Setup calculated data, for optimizing
	//==================================
	m_sizeVertexType = sizeof(VertexType);
}

ParticleRenderer::~ParticleRenderer()
{	
	//OutputDebugString(L"~ParticleRenderer destructor called\n");
	//Shutdown();
}


void ParticleRenderer::CreateDeviceResources()
{
	m_commonStates = new CommonStates(m_d3dDevice.Get());
	SetBlendStateId(m_blendStateId);
	m_depthStencilState = m_commonStates->DepthDefault();

	//==================================
	// Load the texture that is used for the particles.
	//==================================
	bool isTextureLoaded = LoadTexture();
	if (isTextureLoaded)
	{
		CreateResources();
		//OutputDebugString(L"FINAL!!!\n");
		m_loadingComplete = Completed;
	}
}


void ParticleRenderer::CreateWindowSizeDependentResources(float width, float height)
{
	// WVGA portrait: 768/480 = 1.60
	float screenAspect = height / width; 
	
	// WORKS!
	XMMATRIX tmpMatrix = XMMatrixSet(screenAspect, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
	XMStoreFloat4x4(&m_constantBufferData.projection, tmpMatrix); 
		
	// Finally create the view matrix from the three updated vectors.
	XMVECTOR eye = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
	XMVECTOR at = XMVectorSet(0.0f, 0.0f, 0.1f, 0.0f);
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	XMStoreFloat4x4(&m_constantBufferData.view, XMMatrixLookAtRH(eye, at, up));	
}

void ParticleRenderer::Render()
{
	// Only draw the cube once it is loaded (loading is asynchronous).
	if (m_loadingComplete != Completed || m_deletionRequested || m_state != Playing)
	{
		return;
	}

	m_d3dContext->OMSetRenderTargets(
		1,
		m_renderTargetView.GetAddressOf(),
		m_depthStencilView.Get()
		);

	RenderParticleSystem();
}

void ParticleRenderer::RenderParticleSystem()
{
	// Put the vertex and index buffers on the graphics pipeline to prepare them for drawing.
	RenderBuffers();

	SetShaderParameters();

	// Now render the prepared buffers with the shader.
	RenderParticleShader();
}

void ParticleRenderer::CreateResources()
{
	BasicLoader^ loader = ref new BasicLoader(m_d3dDevice.Get());

    D3D11_INPUT_ELEMENT_DESC layoutDesc[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    
	OutputDebugString(L"1. Before VS\n");

    loader->LoadShader(
        L"ParticleVertexShader.cso",
        layoutDesc,
        ARRAYSIZE(layoutDesc),
        &m_vertexShader,
        &m_inputLayout
        );

	//OutputDebugString(L"2. Before PS\n");

	loader->LoadShader(
        L"ParticlePixelShader.cso",
        &m_pixelShader
        );


	//OutputDebugString(L"3. Const Buffer\n");

	// Setup the description of the dynamic matrix constant buffer that is in the vertex shader.
	D3D11_BUFFER_DESC matrixBufferDesc;
	matrixBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	matrixBufferDesc.ByteWidth = sizeof(ViewProjectionConstantBuffer);
	matrixBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	matrixBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	matrixBufferDesc.MiscFlags = 0;
	matrixBufferDesc.StructureByteStride = 0;

	DX::ThrowIfFailed(
		m_d3dDevice->CreateBuffer(
			&matrixBufferDesc,
			nullptr,
			&m_constantBuffer
			)
		);

	//OutputDebugString(L"4. After Const Buffer\n");

	// Set the maximum number of vertices in the vertex array.
	m_vertexCount = m_maxParticles * 4; // Change to 4, to render a quad with 4 vertices, // Use to be: 2 triangles with 3 vertices = 6;

	// Set the maximum number of indices in the index array.
	m_indexCount = m_maxParticles * 6; // indices will determine which vertex will be used for a triangle to make up the quad, // Use to be: m_vertexCount;

	// Create the vertex array for the particles that will be rendered.
	m_vertices = new VertexType[m_vertexCount];
	ASSERT_MSG(m_vertices != nullptr, L"Can't create the vertex array\n");
		
	if (m_vertices != nullptr)
	{
		// Initialize vertex array to zeros at first.
		m_totalSizeVertices = m_sizeVertexType * m_vertexCount;
		memset(m_vertices, 0, m_totalSizeVertices);
		D3D11_SUBRESOURCE_DATA vertexBufferData = {0};
		vertexBufferData.pSysMem = m_vertices;
		vertexBufferData.SysMemPitch = 0;
		vertexBufferData.SysMemSlicePitch = 0;

		int sizeOfMVertices = sizeof(m_vertices);
		CD3D11_BUFFER_DESC vertexBufferDesc(
			m_totalSizeVertices,			// byteWidth
			D3D11_BIND_VERTEX_BUFFER,	// bindFlags
			D3D11_USAGE_DYNAMIC,		// D3D11_USAGE usage = D3D11_USAGE_DEFAULT
			D3D11_CPU_ACCESS_WRITE,		// cpuaccessFlags
			0,							// miscFlags
			0							// structureByteStride
			);

		//OutputDebugString(L"5. Before Vertex Buffer\n");

		DX::ThrowIfFailed(
			m_d3dDevice->CreateBuffer(
				&vertexBufferDesc,
				&vertexBufferData,
				&m_vertexBuffer
				)
			);
	}

	unsigned short * indices = new unsigned short[m_indexCount];
	ASSERT_MSG(indices != nullptr, L"Can't create the index array\n");

	if (indices != nullptr)
	{
		// Initialize the index array.
		unsigned short i6 = 0;
		unsigned short i4 = 0;
		for(unsigned short i = 0; i < m_maxParticles; ++i)
		{
			i6 = i*6;
			i4 = i*4;
			indices[i6+0] = i4+0;
			indices[i6+1] = i4+1;
			indices[i6+2] = i4+2;
			indices[i6+3] = i4+0;
			indices[i6+4] = i4+2;
			indices[i6+5] = i4+3;
		}

		// Set up the description of the static index buffer.
		// Create the index array.
		D3D11_BUFFER_DESC indexBufferDesc;
		indexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
		indexBufferDesc.ByteWidth = sizeof(unsigned short) * m_indexCount;
		indexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		indexBufferDesc.CPUAccessFlags = 0;
		indexBufferDesc.MiscFlags = 0;
		indexBufferDesc.StructureByteStride = 0;

		// Give the subresource structure a pointer to the index data.
		D3D11_SUBRESOURCE_DATA indexData;		
		indexData.pSysMem = indices;
		indexData.SysMemPitch = 0;
		indexData.SysMemSlicePitch = 0;

		//OutputDebugString(L"6. Before Index Buffer\n");

		// Create the index buffer.
		DX::ThrowIfFailed(
			m_d3dDevice->CreateBuffer(
				&indexBufferDesc,
				&indexData,
				&m_indexBuffer
				)
			);
		
		// Release the index array since it is no longer needed.
		delete [] indices;
		indices = nullptr;

	}

}

int ParticleRenderer::GetIndexCount()
{
	return m_indexCount;
}

bool ParticleRenderer::LoadTexture()
{
	HRESULT hr = S_OK;
	
	hr = CreateDDSTextureFromFile(m_d3dDevice.Get(), PARTICLE_TEXTURES[(int)m_particleEffect], nullptr, &m_textureView );

	if (FAILED(hr))
	{
		OutputDebugString(L"FAILED to LoadTexture");
	}

    return (hr == S_OK);	
}

void ParticleRenderer::ReleaseTexture()
{
	// Release the texture object.
	if(m_textureView != nullptr)
	{	
		m_textureView = nullptr;	
	}
}

bool ParticleRenderer::InitParticleProperties(
		LanguageGameWp8DxComponent::ParticleEffect effectId, 
		float startPosX, float startPosY, 
		float devPosX, float devPosY, 
		int maxNumParticles,
		int numParticlesPerSec,
		float angle, float angleVar,
		float speed, float speedVar,
		float startSize, float startSizeVar,
		float middleSize, float middleSizeVar,
		float endSize, float endSizeVar,
		float lifetime, float lifetimeVar,
		float startRed, float startGreen, float startBlue, float startAlpha,
		float startRedVar, float startGreenVar, float startBlueVar, float startAlphaVar,
		float middleRed, float middleGreen, float middleBlue, float middleAlpha,
		float middleRedVar, float middleGreenVar, float middleBlueVar, float middleAlphaVar,
		float endRed, float endGreen, float endBlue, float endAlpha,
		float endRedVar, float endGreenVar, float endBlueVar, float endAlphaVar,		
		float gravityX, float gravityY,
		float radialAccel, float radialAccelVar,
		float tangentialAccel, float tangentialAccelVar,
		float duration,
		BlendStates blendState,
		bool autoPlay,
		float startTime,
		float rotationSpeed,
		float rotationSpeedVar,
		bool enableTextureRotation
		)
{
	//==================================
	// Set Particle's Properties
	//==================================
	m_particleEffect = effectId;
	m_startPosX = startPosX;
	m_startPosY = startPosY;

	// Set the random deviation of where the particles can be located when emitted.
	m_startPosXVar = devPosX;
	m_startPosYVar = devPosY;

	// Set the maximsum number of particles allowed in the particle system.
	m_maxParticles = maxNumParticles;

	// Set the number of particles to emit per second.
	m_emissionRate = numParticlesPerSec;

	m_angle = angle;
	m_angleVar = angleVar;

	// Set the speed and speed variation of particles.
	m_speed = speed; 
	m_speedVar = speedVar;

	// Set the physical size of the particles.
	m_startSize = startSize; 
	m_startSizeVar = startSizeVar;
	m_middleSize = middleSize; 
	m_middleSizeVar = middleSizeVar;
	m_endSize = endSize; 
	m_endSizeVar = endSizeVar;

	if (lifetime < 0.0f)
	{
		m_isPartInfiniteLifetime = true;
	}
	else
	{
		m_isPartInfiniteLifetime = false;
	}

	m_lifetime = abs(lifetime);
	m_lifetimeVar = lifetimeVar;

	// Set start, middle, and end colors
	m_startRed = startRed;
	m_startGreen = startGreen;
	m_startBlue = startBlue;
	m_startAlpha = startAlpha;
	m_startRedVar = startRedVar;
	m_startGreenVar = startGreenVar;
	m_startBlueVar = startBlueVar;
	m_startAlphaVar = startAlphaVar;
	m_middleRed = middleRed;
	m_middleGreen = middleGreen;
	m_middleBlue = middleBlue;
	m_middleAlpha = middleAlpha;
	m_middleRedVar = middleRedVar;
	m_middleGreenVar = middleGreenVar;
	m_middleBlueVar = middleBlueVar;
	m_middleAlphaVar = middleAlphaVar;
	m_endRed = endRed;
	m_endGreen = endGreen;
	m_endBlue = endBlue;
	m_endAlpha = endAlpha;
	m_endRedVar = endRedVar;
	m_endGreenVar = endGreenVar;
	m_endBlueVar = endBlueVar;
	m_endAlphaVar = endAlphaVar;

	m_gravityX = gravityX;
	m_gravityY = gravityY;

	m_radialAccel = radialAccel;
	m_radialAccelVar = radialAccelVar;

	m_tangentialAccel = tangentialAccel;
	m_tangentialAccelVar = tangentialAccelVar;

	m_duration = duration;
	
	m_blendStateId = blendState;

	ONE_OVER_EMISSIONRATE = 1.0f / m_emissionRate;

	if (autoPlay)
	{
		m_state = Playing;
	}
	else
	{
		m_state = Paused;
	}

	m_startTime = startTime;

	m_rotationSpeed = rotationSpeed;
	m_rotationSpeedVar = rotationSpeedVar;

	m_enableTextureRotation = enableTextureRotation;
	ResetParticles();

	return true;
}


void ParticleRenderer::ShutdownParticleSystem()
{
	// Release the particle list.
	if(m_particleList)
	{
		delete [] m_particleList;
		m_particleList = nullptr;
	}
}

void ParticleRenderer::ShutdownBuffers()
{
	// Release the index buffer.
	if(m_indexBuffer)
	{
		m_indexBuffer.Get()->Release();
		m_indexBuffer = nullptr;
	}

	// Release the vertex buffer.
	if(m_vertexBuffer)
	{
		m_vertexBuffer.Get()->Release();
		m_vertexBuffer = nullptr;
	}
	
}


void ParticleRenderer::EmitParticles(float delta)
{
	m_accumulatedTime += delta;

	if (m_startTime > 0.0f)
	{
		// Start playing a paused emitter after m_startTime has passed
		if (m_accumulatedTime >= m_startTime)
		{
			m_state = Playing;
		}
		else
		{
			return;
		}
	}
	else if ((m_duration < 0.0f) || (m_accumulatedTime < m_duration))	// If duration < 0, go into infinity mode, emit particles forever
	{
		m_state = Playing;
	}
	else
	{
		m_state = Finished;
		return;
	}

	if (m_emissionRate > 0.0f) 
	{
		// emit new particles based on how much time has passed and the emission rate
		float rate = ONE_OVER_EMISSIONRATE; //1.0 / m_emissionRate;
		m_elapsedTimeSinceEmitParticle += delta;
		while (		(m_currentParticleCount != m_maxParticles)
				&&	(m_elapsedTimeSinceEmitParticle > rate) )
		{
			AddParticle();
			m_elapsedTimeSinceEmitParticle -= rate;
		}
	}
}

void ParticleRenderer::AddParticle()
{
	// Now generate the randomized particle properties.
	float positionX = m_startPosX + m_startPosXVar * RANDOM_MINUS1_1();
	float positionY = m_startPosY + m_startPosYVar * RANDOM_MINUS1_1();
		
	float angle = m_angle + m_angleVar * RANDOM_MINUS1_1();
	float speed = m_speed + m_speedVar * RANDOM_MINUS1_1();

	float angleRad = DEGREES_TO_RADIANS(angle);
	float velocityX = cosf(angleRad) * speed;
	float velocityY = -sinf(angleRad) * speed;
	
	float red = m_startRed + m_startRedVar * RANDOM_MINUS1_1();
	float green = m_startGreen + m_startGreenVar * RANDOM_MINUS1_1();
	float blue = m_startBlue + m_startBlueVar * RANDOM_MINUS1_1();
	float alpha = m_startAlpha + m_startAlphaVar * RANDOM_MINUS1_1();
	red = clampf(red, 0.0f, 1.0f);	
	green = clampf(red, 0.0f, 1.0f);	
	blue = clampf(red, 0.0f, 1.0f);	
	alpha = clampf(red, 0.0f, 1.0f);	

	float radialAccel = m_radialAccel + m_radialAccelVar * RANDOM_0_1();
	float tangentialAccel = m_tangentialAccel + m_tangentialAccelVar * RANDOM_0_1();
	radialAccel = max(0.0f, radialAccel);
	tangentialAccel = max(0.0f, tangentialAccel);

	float lifetime = m_lifetime + m_lifetimeVar * RANDOM_0_1();
	const float OVER_HALF_LIFETIME = 1.0f / (lifetime * 0.5f);

	float startSize = m_startSize + m_startSizeVar * RANDOM_MINUS1_1();
	float middleSize = m_middleSize + m_middleSizeVar * RANDOM_MINUS1_1();
	float endSize = m_endSize + m_endSizeVar * RANDOM_MINUS1_1();
	startSize = max(startSize, 0.0f);
	middleSize = max(middleSize, 0.0f);
	endSize = max(endSize, 0.0f);
	
	float deltaSize1 = 0.0f;
	float deltaSize2 = 0.0f;

	if (m_startSize != m_middleSize) 
	{
		deltaSize1  = (middleSize - startSize) * OVER_HALF_LIFETIME;
	}
	if (m_endSize != m_middleSize) 
	{
		deltaSize2  = (endSize - middleSize) * OVER_HALF_LIFETIME;
	}

	float startR = m_startRed + m_startRedVar * RANDOM_MINUS1_1();
	float startG = m_startGreen + m_startGreenVar * RANDOM_MINUS1_1();
	float startB = m_startBlue + m_startBlueVar * RANDOM_MINUS1_1();
	float startA = m_startAlpha + m_startAlphaVar * RANDOM_MINUS1_1();

	// if there is no middle or end color, then the particle will end up staying at startColor the whole time
	float middleR = startR;
	float middleG = startG;
	float middleB = startB;
	float middleA = startA;

	float endR = startR;
	float endG = startG;
	float endB = startB;
	float endA = startA;

	float deltaR1 = 0.0f;
	float deltaG1 = 0.0f;
	float deltaB1 = 0.0f;
	float deltaA1 = 0.0f;
	float deltaR2 = 0.0f;
	float deltaG2 = 0.0f;
	float deltaB2 = 0.0f;
	float deltaA2 = 0.0f;

	if (	m_startRed != m_middleRed 
		||	m_startGreen != m_middleGreen 
		||	m_startBlue != m_middleBlue 
		||	m_startAlpha != m_middleAlpha) 
	{
		middleR = m_middleRed + m_middleRedVar * RANDOM_MINUS1_1();
		middleG = m_middleGreen + m_middleGreenVar * RANDOM_MINUS1_1();
		middleB = m_middleBlue + m_middleBlueVar * RANDOM_MINUS1_1();
		middleA = m_middleAlpha + m_middleAlphaVar * RANDOM_MINUS1_1();

		deltaR1 = (middleR - startR) * OVER_HALF_LIFETIME;
		deltaG1 = (middleG - startG) * OVER_HALF_LIFETIME;
		deltaB1 = (middleB - startB) * OVER_HALF_LIFETIME;
		deltaA1 = (middleA - startA) * OVER_HALF_LIFETIME;
	}

	if (	m_middleRed != m_endRed 
		||	m_middleGreen != m_endGreen 
		||	m_middleBlue != m_endBlue 
		||	m_middleAlpha != m_endAlpha) 
	{
		endR = m_endRed + m_endRedVar * RANDOM_MINUS1_1();
		endG = m_endGreen + m_endGreenVar * RANDOM_MINUS1_1();
		endB = m_endBlue + m_endBlueVar * RANDOM_MINUS1_1();
		endA = m_endAlpha + m_endAlphaVar * RANDOM_MINUS1_1();
	
		deltaR2 = (endR - middleR) * OVER_HALF_LIFETIME;
		deltaG2 = (endG - middleG) * OVER_HALF_LIFETIME;
		deltaB2 = (endB - middleB) * OVER_HALF_LIFETIME;
		deltaA2 = (endA - middleA) * OVER_HALF_LIFETIME;
	}
	
	// rotation
	float rotationSpeed = m_rotationSpeed + m_rotationSpeedVar* RANDOM_MINUS1_1();
	rotationSpeed = DEGREES_TO_RADIANS(rotationSpeed);

	int index = m_currentParticleCount;
	++m_currentParticleCount;
	m_particleList[index].positionX = positionX;
	m_particleList[index].positionY = positionY;	

	m_particleList[index].red       = startR;
	m_particleList[index].green     = startG;
	m_particleList[index].blue      = startB;
	m_particleList[index].alpha		= startA;

	m_particleList[index].redDelta1	= deltaR1;
	m_particleList[index].greenDelta1     = deltaG1;
	m_particleList[index].blueDelta1      = deltaB1;
	m_particleList[index].alphaDelta1      = deltaA1;
	
	m_particleList[index].redDelta2	= deltaR2;
	m_particleList[index].greenDelta2     = deltaG2;
	m_particleList[index].blueDelta2      = deltaB2;
	m_particleList[index].alphaDelta2      = deltaA2;
	
	m_particleList[index].velocityX  = velocityX;
	m_particleList[index].velocityY  = velocityY;

	m_particleList[index].size		= startSize;
	m_particleList[index].sizeDelta1 = deltaSize1;
	m_particleList[index].sizeDelta2 = deltaSize2;
	
	m_particleList[index].lifetime  = lifetime;
	m_particleList[index].halfLifeTime  = lifetime * 0.5f;
	m_particleList[index].active    = true;
	m_particleList[index].radialAccel    = radialAccel;
	m_particleList[index].tangentialAccel    = tangentialAccel;

	if (m_enableTextureRotation)
	{
		m_particleList[index].rotation = angleRad; // rotate the texture in this direction, so the particle will move in this direction.
	}
	else
	{
		m_particleList[index].rotation = 0.0f;
	}

	m_particleList[index].rotateSpeed = rotationSpeed;

}


void ParticleRenderer::UpdateParticles(float delta)
{
	// Each frame we update all the particles by making them move downwards using their position, velocity, and the frame time.
	for(int i = 0; i < m_currentParticleCount; ++i)
	{
		ParticleType *particle = &m_particleList[i];
		if (!m_isPartInfiniteLifetime) 
		{
			particle->lifetime -= delta; 
			if (particle->lifetime > 0.0f)
			{
				UpdateParticle(delta, particle);
			}
		}
		else
		{
			particle->lifetime -= delta; 
			if (particle->lifetime > 0.0f)
			{
				UpdateParticle(delta, particle);				
			}
			else
			{
				//DBOUT("RESTART: " << particle->red << ", " << particle->green << ", " << particle->blue << ", " << particle->alpha << "\r\n");
					
				// Ran out of time on previous particle, but continue to update particle by resetting lifetime
				// back to original time
				particle->lifetime = particle->halfLifeTime * 2.0f;

				float temp = -particle->sizeDelta2;
				particle->sizeDelta2 = -particle->sizeDelta1;
				particle->sizeDelta1 = temp;

				temp = -particle->redDelta2;
				particle->redDelta2 = -particle->redDelta1;
				particle->redDelta1 = temp;

				temp = -particle->greenDelta2;
				particle->greenDelta2 = -particle->greenDelta1;
				particle->greenDelta1 = temp;

				temp = -particle->blueDelta2;
				particle->blueDelta2 = -particle->blueDelta1;
				particle->blueDelta1 = temp;

				temp = -particle->alphaDelta2;
				particle->alphaDelta2 = -particle->alphaDelta1;
				particle->alphaDelta1 = temp;

				UpdateParticle(delta, particle);
			}
		}		
	}
}

void ParticleRenderer::UpdateParticle(float delta, ParticleType *particle)
{
	// temp storage
	float forcesX = 0.0f;
	float forcesY = 0.0f;
	float radialX = 0.0f;
	float radialY = 0.0f;
	float tangentialX = 0.0f;
	float tangentialY = 0.0f;

	// dont apply radial forces until moved away from the emitter
	if ((particle->positionX != m_startPosX || particle->positionY != m_startPosY) 
		&& (m_radialAccel > 0.0f || m_tangentialAccel > 0.0f)) 
	{
		radialX = particle->positionX - m_startPosX;
		radialY = particle->positionY - m_startPosY;

		// normalize
		float length = sqrtf(radialX * radialX + radialY * radialY);
		radialX /= length;
		radialY /= length;
	}

	tangentialX = radialX;
	tangentialY = radialY;

	radialX *= particle->radialAccel;
	radialY *= particle->radialAccel;

	float newy = tangentialX;
	tangentialX = -tangentialY;
	tangentialY = newy;

	tangentialX *= particle->tangentialAccel;
	tangentialY *= particle->tangentialAccel;

	forcesX = radialX + tangentialX + m_gravityX;
	forcesY = radialY + tangentialY + m_gravityY;

	forcesX *= delta;
	forcesY *= delta;

	particle->velocityX += forcesX;
	particle->velocityY += forcesY;

	particle->positionX += particle->velocityX * delta;
	particle->positionY += particle->velocityY * delta;

	if (particle->lifetime >= particle->halfLifeTime)
	{
		particle->red += clampf(particle->redDelta1 * delta, -1.0f, 1.0f);
		particle->green += clampf(particle->greenDelta1 * delta, -1.0f, 1.0f);
		particle->blue += clampf(particle->blueDelta1 * delta, -1.0f, 1.0f);
		particle->alpha += clampf(particle->alphaDelta1 * delta, -1.0f, 1.0f);

		particle->size += (particle->sizeDelta1 * delta);
	}
	else
	{
		particle->red += clampf(particle->redDelta2 * delta, -1.0f, 1.0f);
		particle->green += clampf(particle->greenDelta2 * delta, -1.0f, 1.0f);
		particle->blue += clampf(particle->blueDelta2 * delta, -1.0f, 1.0f);
		particle->alpha += clampf(particle->alphaDelta2 * delta, -1.0f, 1.0f);

		particle->size += (particle->sizeDelta2 * delta);			
	}

	particle->red = clampf(particle->red, 0.0f, 1.0f);
	particle->green = clampf(particle->green, 0.0f, 1.0f);
	particle->blue = clampf(particle->blue, 0.0f, 1.0f);
	particle->alpha = clampf(particle->alpha, 0.0f, 1.0f);

	particle->size = max(0, particle->size);

	// Continuous rotation in a circle based on speed in radians
	particle->rotation += (particle->rotateSpeed * delta);
	if (particle->rotation > TWO_PI_F)
	{
		particle->rotation -= TWO_PI_F;
	}
}

void ParticleRenderer::KillParticles()
{
	// Kill all the particles that have gone below a certain height range.
	int i = 0;
	while (i < m_maxParticles)
	{
		if(m_particleList != nullptr && (m_particleList[i].active == true) && (m_particleList[i].lifetime <= 0.0f))
		{
			m_particleList[i].active = false;
			--m_currentParticleCount;
			
			// Swap the last particle to the newly inactive particle at index i
			m_particleList[i] = m_particleList[m_currentParticleCount];
			m_particleList[m_currentParticleCount].active = false;

			// Don't increment i, as we need to next check this newly swapped in particle at i
		}
		else
		{
			++i;
		}
	}

}

bool ParticleRenderer::Update(float timeTotal, float timeDelta)
{
	if (m_deletionRequested)
	{
		// Try to delete particles
		Shutdown();
	}
	else if (m_loadingComplete == Completed)
	{
		// Only draw the particles once it is loaded (loading is asynchronous).
		Frame(timeTotal, timeDelta);		
	}

	return IsParticlesUpdating();
}

void ParticleRenderer::Frame(float frameTime, float deltaTime)
{
	// Release old particles.
	KillParticles();
	
	// Emit new particles.
	if (m_state == Playing)
	{
		EmitParticles(deltaTime);

		// Update the position of the particles.
		UpdateParticles(deltaTime);

		// Update the dynamic vertex buffer with the new position of each particle.
		UpdateBuffers();	
	}	
}

bool ParticleRenderer::UpdateBuffers()
{
	// Initialize vertex array to zeros at first.
	memset(m_vertices, 0, m_totalSizeVertices);

	// Now build the vertex array from the particle list array.  Each particle is a quad made out of two triangles.
	int index = 0;
	XMFLOAT4 color;
	for(int i = 0; i < m_currentParticleCount; ++i)
	{
		ParticleType particle = m_particleList[i];
		color = XMFLOAT4(particle.red, particle.green, particle.blue, particle.alpha);

		if (!m_enableTextureRotation)
		{
			// Draw a Quad with position, texture, and color
			// Bottom right.
			m_vertices[index].position =  XMFLOAT2(particle.positionX + particle.size, particle.positionY - particle.size);
			m_vertices[index].texture =  XMFLOAT2(1.0f, 1.0f);
			m_vertices[index].color = color;
			index++;

			// Bottom left.
			m_vertices[index].position = XMFLOAT2(particle.positionX - particle.size, particle.positionY - particle.size);
			m_vertices[index].texture =  XMFLOAT2(0.0f, 1.0f);
			m_vertices[index].color = color;
			index++;
				
			// Top left.
			m_vertices[index].position =  XMFLOAT2(particle.positionX - particle.size, particle.positionY + particle.size);
			m_vertices[index].texture =  XMFLOAT2(0.0f, 0.0f);
			m_vertices[index].color = color;
			index++;
		
			// Top right.
			m_vertices[index].position =  XMFLOAT2(particle.positionX + particle.size, particle.positionY + particle.size);
			m_vertices[index].texture =  XMFLOAT2(1.0f, 0.0f);
			m_vertices[index].color = color;
			index++;
		}
		else
		{
			// Code from Cocos2dx CCParticleSystemQuad.cpp updateQuadWithParticle()
			float size_2 = particle.size;
			float x1 = -size_2;
			float y1 = -size_2;

			float x2 = size_2;
			float y2 = size_2;
			float x = particle.positionX;
			float y = particle.positionY;

			float r = (float)-(particle.rotation);
			float cr = cosf(r);
			float sr = sinf(r);
			float ax = x1 * cr - y1 * sr + x;
			float ay = x1 * sr + y1 * cr + y;
			float bx = x2 * cr - y1 * sr + x;
			float by = x2 * sr + y1 * cr + y;
			float cx = x2 * cr - y2 * sr + x;
			float cy = x2 * sr + y2 * cr + y;
			float dx = x1 * cr - y2 * sr + x;
			float dy = x1 * sr + y2 * cr + y;

			// Bottom right.
			m_vertices[index].position =  XMFLOAT2(bx, by);
			m_vertices[index].texture =  XMFLOAT2(1.0f, 1.0f);
			m_vertices[index].color = color;
			index++;

			// Bottom left.
			m_vertices[index].position = XMFLOAT2(ax, ay);
			m_vertices[index].texture =  XMFLOAT2(0.0f, 1.0f);
			m_vertices[index].color = color;
			index++;
				
			// Top left.
			m_vertices[index].position =  XMFLOAT2(dx, dy);
			m_vertices[index].texture =  XMFLOAT2(0.0f, 0.0f);
			m_vertices[index].color = color;
			index++;
		
			// Top right.
			m_vertices[index].position =  XMFLOAT2(cx, cy);
			m_vertices[index].texture =  XMFLOAT2(1.0f, 0.0f);
			m_vertices[index].color = color;
			index++;
		}
		
	}

	DirectX::XMMATRIX sample = XMMatrixIdentity();


	D3D11_MAPPED_SUBRESOURCE mappedResource;	
	
	// Lock the vertex buffer.
	DX::ThrowIfFailed(m_d3dContext->Map(m_vertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource));

	// Get a pointer to the data in the vertex buffer.
	VertexType * verticesPtr = (VertexType*)mappedResource.pData;

	// Copy the data into the vertex buffer.
	memcpy(verticesPtr, (void*)m_vertices, m_totalSizeVertices);

	// Unlock the vertex buffer.
	m_d3dContext->Unmap(m_vertexBuffer.Get(), 0);

	return true;
}

void ParticleRenderer::SetShaderParameters()
{
	ViewProjectionConstantBuffer* dataPtr;
	
	D3D11_MAPPED_SUBRESOURCE mappedResource;
	DX::ThrowIfFailed(m_d3dContext->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource));

	dataPtr = (ViewProjectionConstantBuffer*)mappedResource.pData;
	dataPtr->view = m_constantBufferData.view;
	dataPtr->projection = m_constantBufferData.projection;
	
	m_d3dContext->Unmap(m_constantBuffer.Get(), 0);

	// Now set the constant buffer in the vertex shader with the updated values.
	m_d3dContext->VSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf() );

	// Set shader texture resource in the pixel shader.
	m_d3dContext->PSSetShaderResources(0, 1, m_textureView.GetAddressOf());	
}

void ParticleRenderer::RenderParticleShader()
{	
	// Set the vertex input layout.
	m_d3dContext->IASetInputLayout(m_inputLayout.Get());

    // Set the vertex and pixel shaders that will be used to render this triangle.
    m_d3dContext->VSSetShader(
		m_vertexShader.Get(),
		nullptr,
		0
		);

	m_d3dContext->PSSetShader(
		m_pixelShader.Get(),
		nullptr,
		0
		);

	// Set the blend and depth stencil state.
    m_d3dContext->OMSetBlendState(m_blendState, nullptr, 0xFFFFFFFF);
    m_d3dContext->OMSetDepthStencilState(m_depthStencilState, 0);
	auto samplerState = m_commonStates->LinearWrap();
    m_d3dContext->PSSetSamplers(0, 1, &samplerState);

	// Render the triangle.
	m_d3dContext->DrawIndexed(m_indexCount, 0, 0);
}

void ParticleRenderer::RenderBuffers()
{
	// Set vertex buffer stride and offset.
    unsigned int stride = m_sizeVertexType; 
	unsigned int offset = 0;
    
	// Set the vertex buffer to active in the input assembler so it can be rendered.
	m_d3dContext->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);

    // Set the index buffer to active in the input assembler so it can be rendered.
    m_d3dContext->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);

    // Set the type of primitive that should be rendered from this vertex buffer.
    m_d3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void ParticleRenderer::Shutdown()
{
	m_state = Finished;
	
	// only delete if completed loading.
	if (m_loadingComplete == Completed)
	{
		OutputDebugString(L"Shutdown\n");

		// Release the buffers.
		//ShutdownBuffers();

		// Release the particle system.
		ShutdownParticleSystem();

		// Release the texture used for the particles.
		ReleaseTexture();

		m_particleFilePath = nullptr;

		m_loadingComplete = DoneShutdown;

		m_deletionRequested = false;
		
	}
	
}

void ParticleRenderer::ForceShutdown()
{
	OutputDebugString(L"ForceShutdown\n");
	m_state = Finished;
	
	// Release the buffers.
	//ShutdownBuffers();

	// Release the particle system.
	ShutdownParticleSystem();

	// Release the texture used for the particles.
	ReleaseTexture();

	m_particleFilePath = nullptr;

	m_loadingComplete = DoneShutdown;

	m_deletionRequested = false;
	
}

void ParticleRenderer::ResetParticles()
{
	m_elapsedTimeSinceEmitParticle = 0.0f;
	m_accumulatedTime = 0.0f;	

	// Initialize the current particle count to zero since none are emitted yet.
	m_currentParticleCount = 0;

	ShutdownParticleSystem();

	m_particleList = new ParticleType[m_maxParticles];
	if(!m_particleList)
	{
		OutputDebugString(L"Can't create particle list");
		assert(true);
	}

	// Initialize the particle list.
	for (int i = 0; i < m_maxParticles; ++i)
	{
		m_particleList[i].active = false;
	}

}

float ParticleRenderer::GetDuration()
{
	return m_duration;
}

void ParticleRenderer::SetDuration(float var)
{
	m_duration = var;
	m_elapsedTimeSinceEmitParticle = 0.0f;
	m_accumulatedTime = 0.0f;
	m_state = Playing;
}

int ParticleRenderer::GetEmissionRate()
{
	return m_emissionRate;
}

void ParticleRenderer::SetEmissionRate(int var)
{
	m_emissionRate = var;
	ONE_OVER_EMISSIONRATE = 1.0f / m_emissionRate;
}

float ParticleRenderer::GetLifetime()
{
	if (m_isPartInfiniteLifetime)
	{
		return -m_lifetime;
	}

	return m_lifetime;
}
void ParticleRenderer::SetLifetime(float var)
{
	if (var < 0.0f)
	{
		m_isPartInfiniteLifetime = true;
	}
	else
	{
		m_isPartInfiniteLifetime = false;
	}

	m_lifetime = abs(var);
}

float ParticleRenderer::GetStartTime()
{
	return m_startTime;
}
void ParticleRenderer::SetStartTime(float var)
{
	m_accumulatedTime = 0.0f; // reset
	m_startTime = var;
}

ParticleEffect ParticleRenderer::GetParticleEffectId()
{
	return m_particleEffect;
}

void ParticleRenderer::SetParticleEffectId(ParticleEffect effectId)
{
	if (m_particleEffect != effectId)
	{
		m_particleEffect = effectId;
		CreateDeviceResources();
	}
}

int ParticleRenderer::GetMaxParticles()
{
	return m_maxParticles;
}

void ParticleRenderer::SetMaxParticles(int var, bool reload)
{
	m_maxParticles = var;

	if (reload)
	{
		m_loadingComplete = Idle;
		m_state = Playing;
		ResetParticles();	
		CreateDeviceResources();
	}
}

BlendStates ParticleRenderer::GetBlendStateId()
{
	return m_blendStateId;
}


void ParticleRenderer::SetBlendStateId(BlendStates state)
{
	if (m_commonStates)
	{
		m_blendStateId = state;
		if (state == BlendStates::Additive)
		{
			m_blendState = m_commonStates->Additive();
		}
		else if (state == BlendStates::AlphaBlend)
		{
			m_blendState = m_commonStates->AlphaBlend();
		}
		else if (state == BlendStates::NonPremultiplied)
		{
			m_blendState = m_commonStates->NonPremultiplied();
		}
		else if (state == BlendStates::Opaque)
		{
			m_blendState = m_commonStates->Opaque();
		}
		else
		{
			OutputDebugString(L"Can't find requested blend state id.");
			assert(true);
		}
	}
}

bool ParticleRenderer::IsParticlesUpdating()
{
	return (m_state != Finished);
}

void ParticleRenderer::PlayParticle()
{
	m_state = Playing;
}

void ParticleRenderer::SetDeletionRequested(bool value)
{
	if (m_loadingComplete != DoneShutdown)
	{
		m_deletionRequested = value;
	}
}

bool ParticleRenderer::GetDeletionRequested()
{
	return m_deletionRequested;
}

void ParticleRenderer::Pause()
{
	m_state = Paused;
}

void ParticleRenderer::Play(bool reset)
{
	if (reset)
	{
		ResetParticles();
	}
	m_state = Playing;
}

bool ParticleRenderer::IsLoaded()
{
	return (m_loadingComplete == LoadState::Completed);
}