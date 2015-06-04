#pragma once

#include "CommonStates.h"
#include "ParticleEnums.h"
#include "Engine\Common\BasicLoader.h"


namespace LanguageGameWp8DxComponent
{	
	public enum class BlendStates
	{
		Additive,
		Opaque,
		AlphaBlend,
		NonPremultiplied,

		NumBlendStates,
		Default = Additive
	};

	public ref class BlendStatesEnum sealed
	{
	public:
		property BlendStates BlendId
		{
			BlendStates  get()
			{
				return m_blendState; 
			}
		}

	private:
		BlendStates m_blendState;
	};

}
struct ViewProjectionConstantBuffer
{
	DirectX::XMFLOAT4X4 view;
	DirectX::XMFLOAT4X4 projection;
};

struct ParticleType
{
	float positionX, positionY;

	float red, green, blue, alpha;	// Current color value drawn
	float redDelta1, greenDelta1, blueDelta1, alphaDelta1;	// From Start to Middle
	float redDelta2, greenDelta2, blueDelta2, alphaDelta2;	// From Middle to End
	
	float velocityX, velocityY;
	float size, sizeDelta1, sizeDelta2;
	float lifetime, halfLifeTime;
	bool active;
	float radialAccel;
	float tangentialAccel;
	float rotation;		// direction (-/+) and current angle
	float rotateSpeed;	// Scalar value to change rotation value
};

struct VertexType
{
	DirectX::XMFLOAT2 position;
	DirectX::XMFLOAT2 texture;
	DirectX::XMFLOAT4 color;		
};

#define PROPERTY_DEFINE_MEMBER(varType, varName) private: varType varName

#define PROPERTY_DEFINE_FUNC(varType, varName, funcName) \
public: varType Get##funcName(void) { return varName; } \
public: void Set##funcName(varType var) { varName = var; } \

#define PROPERTY_DEFINE(varType, varName, funcName) \
private: varType varName; \
public: varType Get##funcName(void) { return varName; } \
public: void Set##funcName(varType var) { varName = var; } \

// This class renders a particle emitter.
class ParticleRenderer
{
public:

	ParticleRenderer(
		Microsoft::WRL::ComPtr<ID3D11Device1> d3dDevice, 
		Microsoft::WRL::ComPtr<ID3D11DeviceContext1> d3dContext,
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTargetView,
		Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depthStencilView);

	~ParticleRenderer();
	
	enum State
	{
		Paused,		// particles are not emitted, can play particle, to be active
		Playing,	// active particles playing/emitting
		Finished	// inactive, finished playing particles, can't emit any more particles, ready for deletion
	};

	enum LoadState
	{
		Idle,
		Loading,
		Completed,
		DoneShutdown
	};

	// Direct3DBase methods.
	void CreateDeviceResources();
	void CreateWindowSizeDependentResources(float width, float height);
	void Render();
	
	// Method for updating time-dependent objects.
	// Return m_active, so we know if the particle emitter is completed and can be deleted.
	bool Update(float timeTotal, float timeDelta);
	
	bool InitParticleProperties(
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
		LanguageGameWp8DxComponent::BlendStates blendState,
		bool autoPlay,
		float startTime,
		float rotationSpeed, float rotationSpeedVar,
		bool enableTextureRotation
		);

	void ResetParticles();
	void ShutdownParticleSystem();
	bool IsParticlesUpdating();
	void PlayParticle();
	void Shutdown();
	void SetDeletionRequested(bool value);
	bool GetDeletionRequested();
	void Pause();
	void Play(bool reset);
	bool IsLoaded();
	void ForceShutdown();

private:

	Platform::String^ m_particleFilePath;
	LoadState m_loadingComplete;
	bool m_deletionRequested;

	Microsoft::WRL::ComPtr<ID3D11Device1> m_d3dDevice;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext1> m_d3dContext;
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_renderTargetView;
	Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_depthStencilView;

	

	Microsoft::WRL::ComPtr<ID3D11InputLayout> m_inputLayout;
	Microsoft::WRL::ComPtr<ID3D11Buffer> m_vertexBuffer;
	Microsoft::WRL::ComPtr<ID3D11Buffer> m_indexBuffer;
	Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShader;
	Microsoft::WRL::ComPtr<ID3D11PixelShader> m_pixelShader;
	Microsoft::WRL::ComPtr<ID3D11Buffer> m_constantBuffer;
	ViewProjectionConstantBuffer m_constantBufferData;
	
	Microsoft::WRL::ComPtr<ID3D11SamplerState> m_sampleState;
	void InitParticleSystem();
	void RenderParticleSystem();
	//Concurrency::task<void> CreateParticleResources(Concurrency::task<Platform::Array<byte>^> loadVSTask, Concurrency::task<Platform::Array<byte>^> loadPSTask);
	//Concurrency::task<void> CreateParticleResources();
	void CreateResources();
		
	void CreateParticleResources(BasicLoader^ loader);
	//================================================
	// For particle system update
	//================================================
	int m_currentParticleCount;
	float m_accumulatedTime; // in seconds
	float m_elapsedTimeSinceEmitParticle;
	State m_state;
	
	ParticleType* m_particleList;
	int m_vertexCount;
	int m_indexCount;
	VertexType* m_vertices;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_textureView;

	ID3D11BlendState* m_blendState;
    ID3D11DepthStencilState* m_depthStencilState;
	
	// Store results of calculations commonly used
	int m_totalSizeVertices;
	int m_sizeVertexType;
	float ONE_OVER_EMISSIONRATE;
	
	DirectX::CommonStates * m_commonStates;
		
	void Frame(float frameTime, float deltaTime);
	void SetShaderParameters(); 

	int GetIndexCount();

	Concurrency::task<void> LoadTexture(BasicLoader^ basicLoader);

	bool LoadTexture();
	void ReleaseTexture();

	bool InitializeParticleSystem();

	void ShutdownBuffers();

	void EmitParticles(float);
	void UpdateParticles(float deltaTime);
	void UpdateParticle(float delta, ParticleType *particle);
	void KillParticles();
	void AddParticle();

	bool UpdateBuffers();
	void RenderBuffers();
	void RenderParticleShader();

//#ifdef DEBUG // For tuning variables to change on the fly
	PROPERTY_DEFINE(float, m_startPosX, StartPosX);
	PROPERTY_DEFINE(float, m_startPosY, StartPosY);
	PROPERTY_DEFINE(float, m_startPosXVar, StartPosXVar);
	PROPERTY_DEFINE(float, m_startPosYVar, StartPosYVar);
	PROPERTY_DEFINE(float, m_speed, Speed);
	PROPERTY_DEFINE(float, m_speedVar, SpeedVar);
	PROPERTY_DEFINE(float, m_startSize, StartSize);
	PROPERTY_DEFINE(float, m_startSizeVar, StartSizeVar);
	PROPERTY_DEFINE(float, m_middleSize, MiddleSize);
	PROPERTY_DEFINE(float, m_middleSizeVar, MiddleSizeVar);
	PROPERTY_DEFINE(float, m_endSize, EndSize);
	PROPERTY_DEFINE(float, m_endSizeVar, EndSizeVar);
	PROPERTY_DEFINE(float, m_angle, Angle);	// in degrees
	PROPERTY_DEFINE(float, m_angleVar, AngleVar);
	PROPERTY_DEFINE(float, m_startRed, StartRed);
	PROPERTY_DEFINE(float, m_startGreen, StartGreen);
	PROPERTY_DEFINE(float, m_startBlue, StartBlue);
	PROPERTY_DEFINE(float, m_startAlpha, StartAlpha);
	PROPERTY_DEFINE(float, m_startRedVar, StartRedVar);
	PROPERTY_DEFINE(float, m_startGreenVar, StartGreenVar);
	PROPERTY_DEFINE(float, m_startBlueVar, StartBlueVar);
	PROPERTY_DEFINE(float, m_startAlphaVar, StartAlphaVar);
	PROPERTY_DEFINE(float, m_middleRed, MiddleRed);
	PROPERTY_DEFINE(float, m_middleGreen, MiddleGreen);
	PROPERTY_DEFINE(float, m_middleBlue, MiddleBlue);
	PROPERTY_DEFINE(float, m_middleAlpha, MiddleAlpha);
	PROPERTY_DEFINE(float, m_middleRedVar, MiddleRedVar);
	PROPERTY_DEFINE(float, m_middleGreenVar, MiddleGreenVar);
	PROPERTY_DEFINE(float, m_middleBlueVar, MiddleBlueVar);
	PROPERTY_DEFINE(float, m_middleAlphaVar, MiddleAlphaVar);	
	PROPERTY_DEFINE(float, m_endRed, EndRed);
	PROPERTY_DEFINE(float, m_endGreen, EndGreen);
	PROPERTY_DEFINE(float, m_endBlue, EndBlue);
	PROPERTY_DEFINE(float, m_endAlpha, EndAlpha);
	PROPERTY_DEFINE(float, m_endRedVar, EndRedVar);
	PROPERTY_DEFINE(float, m_endGreenVar, EndGreenVar);
	PROPERTY_DEFINE(float, m_endBlueVar, EndBlueVar);
	PROPERTY_DEFINE(float, m_endAlphaVar, EndAlphaVar);
	PROPERTY_DEFINE(float, m_gravityX, GravityX);
	PROPERTY_DEFINE(float, m_gravityY, GravityY);
	PROPERTY_DEFINE(float, m_radialAccel, RadialAccel);
	PROPERTY_DEFINE(float, m_radialAccelVar, RadialAccelVar);
	PROPERTY_DEFINE(float, m_tangentialAccel, TangentialAccel);
	PROPERTY_DEFINE(float, m_tangentialAccelVar, TangentialAccelVar);
	PROPERTY_DEFINE(float, m_lifetimeVar, LifetimeVar);
	PROPERTY_DEFINE(float, m_rotationSpeed, RotationSpeed);
	PROPERTY_DEFINE(float, m_rotationSpeedVar, RotationSpeedVar);
	PROPERTY_DEFINE(bool, m_enableTextureRotation, EnableTextureRotation);

	
private:
	
	int m_maxParticles;
	int m_emissionRate;
	float m_duration;
	float m_lifetime;
	float m_startTime;	// in seconds
	LanguageGameWp8DxComponent::ParticleEffect m_particleEffect;
	LanguageGameWp8DxComponent::BlendStates m_blendStateId;
	bool m_isPartInfiniteLifetime;
public:

	float GetDuration();
	void SetDuration(float var);

	int GetMaxParticles();
	void SetMaxParticles(int var, bool reload);

	int GetEmissionRate();
	void SetEmissionRate(int var);

	float GetLifetime();
	void SetLifetime(float var);

	// if this emitter is not auto play, then we will start the emitter after this time has passed by
	float GetStartTime();
	void SetStartTime(float var);
	
	LanguageGameWp8DxComponent::ParticleEffect GetParticleEffectId();
	void SetParticleEffectId(LanguageGameWp8DxComponent::ParticleEffect effectId);

	LanguageGameWp8DxComponent::BlendStates GetBlendStateId();
	void SetBlendStateId(LanguageGameWp8DxComponent::BlendStates state);
	
};
