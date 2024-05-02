//
// Game.cpp
//

#include "pch.h"
#include "Game.h"


//toreorganise
#include <fstream>

extern void ExitGame();

using namespace DirectX;
using namespace DirectX::SimpleMath;
using namespace ImGui;

using Microsoft::WRL::ComPtr;

namespace
{
    struct VS_BLOOM_PARAMETERS
    {
        float bloomThreshold;
        float blurAmount;
        float bloomIntensity;
        float baseIntensity;
        float bloomSaturation;
        float baseSaturation;
        uint8_t na[8];
    };

    static_assert(!(sizeof(VS_BLOOM_PARAMETERS) % 16),
        "VS_BLOOM_PARAMETERS needs to be 16 bytes aligned");

    struct VS_BLUR_PARAMETERS
    {
        static constexpr size_t SAMPLE_COUNT = 15;

        XMFLOAT4 sampleOffsets[SAMPLE_COUNT];
        XMFLOAT4 sampleWeights[SAMPLE_COUNT];

        void SetBlurEffectParameters(float dx, float dy,
            const VS_BLOOM_PARAMETERS& params)
        {
            sampleWeights[0].x = ComputeGaussian(0, params.blurAmount);
            sampleOffsets[0].x = sampleOffsets[0].y = 0.f;

            float totalWeights = sampleWeights[0].x;

            // Add pairs of additional sample taps, positioned
            // along a line in both directions from the center.
            for (size_t i = 0; i < SAMPLE_COUNT / 2; i++)
            {
                // Store weights for the positive and negative taps.
                float weight = ComputeGaussian(float(i + 1.f), params.blurAmount);

                sampleWeights[i * 2 + 1].x = weight;
                sampleWeights[i * 2 + 2].x = weight;

                totalWeights += weight * 2;

                // To get the maximum amount of blurring from a limited number of
                // pixel shader samples, we take advantage of the bilinear filtering
                // hardware inside the texture fetch unit. If we position our texture
                // coordinates exactly halfway between two texels, the filtering unit
                // will average them for us, giving two samples for the price of one.
                // This allows us to step in units of two texels per sample, rather
                // than just one at a time. The 1.5 offset kicks things off by
                // positioning us nicely in between two texels.
                float sampleOffset = float(i) * 2.f + 1.5f;

                Vector2 delta = Vector2(dx, dy) * sampleOffset;

                // Store texture coordinate offsets for the positive and negative taps.
                sampleOffsets[i * 2 + 1].x = delta.x;
                sampleOffsets[i * 2 + 1].y = delta.y;
                sampleOffsets[i * 2 + 2].x = -delta.x;
                sampleOffsets[i * 2 + 2].y = -delta.y;
            }

            for (size_t i = 0; i < SAMPLE_COUNT; i++)
            {
                sampleWeights[i].x /= totalWeights;
            }
        }

    private:
        float ComputeGaussian(float n, float theta)
        {
            return (float)((1.0 / sqrtf(2 * XM_PI * theta))
                * expf(-(n * n) / (2 * theta * theta)));
        }
    };

    static_assert(!(sizeof(VS_BLUR_PARAMETERS) % 16),
        "VS_BLUR_PARAMETERS needs to be 16 bytes aligned");

    enum BloomPresets
    {
        Default = 0,
        Soft,
        Desaturated,
        Saturated,
        Blurry,
        Subtle,
        None
    };

    BloomPresets g_Bloom = Soft;

    static const VS_BLOOM_PARAMETERS g_BloomPresets[] =
    {
        //Thresh  Blur Bloom  Base  BloomSat BaseSat
        { 0.25f,  4,   1.25f, 1,    1,       1 }, // Default
        { 0,      3,   1,     1,    1,       1 }, // Soft
        { 0.5f,   8,   2,     1,    0,       1 }, // Desaturated
        { 0.25f,  4,   2,     1,    2,       0 }, // Saturated
        { 0,      2,   1,     0.1f, 1,       1 }, // Blurry
        { 0.5f,   2,   1,     1,    1,       1 }, // Subtle
        { 0.25f,  4,   1.25f, 1,    1,       1 }, // None
    };
}


Game::Game() noexcept(false)
{
    m_deviceResources = std::make_unique<DX::DeviceResources>();
    m_deviceResources->RegisterDeviceNotify(this);

    const auto format = m_deviceResources->GetBackBufferFormat();
}


Game::~Game()
{
#ifdef DXTK_AUDIO
    if (m_audEngine)
    {
        m_audEngine->Suspend();
    }
#endif
}

// Initialize the Direct3D resources required to run.
void Game::Initialize(HWND window, int width, int height)
{

	m_input.Initialise(window);

    m_deviceResources->SetWindow(window, width, height);

    m_deviceResources->CreateDeviceResources();
    CreateDeviceDependentResources();

    m_deviceResources->CreateWindowSizeDependentResources();
    CreateWindowSizeDependentResources();

	//setup imgui.  its up here cos we need the window handle too
	//pulled from imgui directx11 example
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	ImGui::StyleColorsDark();
	ImGui_ImplWin32_Init(window);		//tie to our window
	ImGui_ImplDX11_Init(m_deviceResources->GetD3DDevice(), m_deviceResources->GetD3DDeviceContext());	//tie to directx

	m_fullscreenRect.left = 0;
	m_fullscreenRect.top = 0;
	m_fullscreenRect.right = 800;
	m_fullscreenRect.bottom = 600;

	m_CameraViewRect.left = 500;
	m_CameraViewRect.top = 0;
	m_CameraViewRect.right = 800;
	m_CameraViewRect.bottom = 240;

	//setup light
	m_Light.setAmbientColour(0.3f, 0.3f, 0.3f, 1.0f);
	m_Light.setDiffuseColour(1.0f, 1.0f, 1.0f, 1.0f);
	m_Light.setPosition(2.0f, 1.0f, 1.0f);
	m_Light.setDirection(-1.0f, -1.0f, 0.0f);

	//setup camera
	m_Camera01.setPosition(Vector3(0.0f, 0.0f, 4.0f));
	m_Camera01.setRotation(Vector3(-90.0f, -180.0f, 0.0f));	//orientation is -90 becuase zero will be looking up at the sky straight up. 

#ifdef DXTK_AUDIO
    // Create DirectXTK for Audio objects
    AUDIO_ENGINE_FLAGS eflags = AudioEngine_Default;
#ifdef _DEBUG
    eflags = eflags | AudioEngine_Debug;
#endif

    m_audEngine = std::make_unique<AudioEngine>(eflags);

    m_audioEvent = 0;
    m_audioTimerAcc = 10.f;
    m_retryDefault = false;

    m_waveBank = std::make_unique<WaveBank>(m_audEngine.get(), L"adpcmdroid.xwb");

    m_soundEffect = std::make_unique<SoundEffect>(m_audEngine.get(), L"MusicMono_adpcm.wav");
    m_effect1 = m_soundEffect->CreateInstance();
    m_effect2 = m_waveBank->CreateInstance(10);

    m_effect1->Play(true);
    m_effect2->Play();
#endif
}

#pragma region Frame Update
// Executes the basic game loop.
void Game::Tick()
{
	//take in input
	m_input.Update();								//update the hardware
	m_gameInputCommands = m_input.getGameInput();	//retrieve the input for our game
	
	//Update all game objects
    m_timer.Tick([&]()
    {
        Update(m_timer);
    });

	//Render all game content. 
    Render();

#ifdef DXTK_AUDIO
    // Only update audio engine once per frame
    if (!m_audEngine->IsCriticalError() && m_audEngine->Update())
    {
        // Setup a retry in 1 second
        m_audioTimerAcc = 1.f;
        m_retryDefault = true;
    }
#endif

	
}

// Updates the world.
void Game::Update(DX::StepTimer const& timer)
{	
	//this is hacky,  i dont like this here.  
	auto device = m_deviceResources->GetD3DDevice();

	//note that currently.  Delta-time is not considered in the game object movement. 
	if (m_gameInputCommands.left)
	{
		Vector3 rotation = m_Camera01.getRotation();
		rotation.y = rotation.y += m_Camera01.getRotationSpeed();
		m_Camera01.setRotation(rotation);
	}
	if (m_gameInputCommands.right)
	{
		Vector3 rotation = m_Camera01.getRotation();
		rotation.y = rotation.y -= m_Camera01.getRotationSpeed();
		m_Camera01.setRotation(rotation);
	}
	if (m_gameInputCommands.forward)
	{
		Vector3 position = m_Camera01.getPosition(); //get the position
		position += (m_Camera01.getForward()*m_Camera01.getMoveSpeed()); //add the forward vector
		m_Camera01.setPosition(position);
	}
	if (m_gameInputCommands.back)
	{
		Vector3 position = m_Camera01.getPosition(); //get the position
		position -= (m_Camera01.getForward()*m_Camera01.getMoveSpeed()); //add the forward vector
		m_Camera01.setPosition(position);
	}

	if (m_gameInputCommands.generate)
	{
		m_Terrain.GenerateHeightMap(device);
	}
    
    if (m_gameInputCommands.smooth)
    {
        m_Terrain.SmoothTerrain(device);
    }

	//m_Terrain.GenerateHeightMap(device);

	m_Camera01.Update();	//camera update.s
	m_view = m_Camera01.getCameraMatrix();
	m_world = Matrix::Identity;

	/*create our UI*/
	SetupGUI();

#ifdef DXTK_AUDIO
    m_audioTimerAcc -= (float)timer.GetElapsedSeconds();
    if (m_audioTimerAcc < 0)
    {
        if (m_retryDefault)
        {
            m_retryDefault = false;
            if (m_audEngine->Reset())
            {
                // Restart looping audio
                m_effect1->Play(true);
            }
        }
        else
        {
            m_audioTimerAcc = 4.f;

            m_waveBank->Play(m_audioEvent++);

            if (m_audioEvent >= 11)
                m_audioEvent = 0;
        }
    }
#endif
   

	if (m_input.Quit())
	{
		ExitGame();
	}
}
#pragma endregion

#pragma region Frame Render
// Draws the scene.
void Game::Render()
{	
    // Don't try to render anything before the first Update.
    if (m_timer.GetFrameCount() == 0)
    {
        return;
    }

    Clear();

    m_deviceResources->PIXBeginEvent(L"Render");
    auto context = m_deviceResources->GetD3DDeviceContext();
	auto renderTargetView = m_deviceResources->GetRenderTargetView();
	auto depthTargetView = m_deviceResources->GetDepthStencilView();

    // Draw Text to the screen
    m_sprites->Begin();
		m_font->DrawString(m_sprites.get(), L"Procedural Methods", XMFLOAT2(10, 10), Colors::Yellow);
    m_sprites->End();

	//Set Rendering states. 
	context->OMSetBlendState(m_states->Opaque(), nullptr, 0xFFFFFFFF);
	context->OMSetDepthStencilState(m_states->DepthDefault(), 0);
	context->RSSetState(m_states->CullClockwise());
	//context->RSSetState(m_states->Wireframe());


	//RenderTexturePass1();
    
    //GenerateVolumetricFogTexture(&m_Cloud);

	//prepare transform for floor object. 
	m_world = SimpleMath::Matrix::Identity; //set world back to identity
	SimpleMath::Matrix newPosition3 = SimpleMath::Matrix::CreateTranslation(0.0f, -0.6f, 0.0f);
	SimpleMath::Matrix newScale = SimpleMath::Matrix::CreateScale(0.1);
    SimpleMath::Matrix newRotation = SimpleMath::Matrix::CreateRotationX(0);//scale the terrain down a little. 
	m_world = m_world * newScale *newPosition3 * newRotation;

	//setup and draw cube
	m_BasicShaderPair.EnableShader(context);
	m_BasicShaderPair.SetShaderParameters(context, &m_world, &m_view, &m_projection, &m_Light, m_texture1.Get());
	m_Terrain.Render(context);
	
	//render our GUI
	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    RenderTexturePass1();
    PostProcess();

	m_sprites->Begin();
	m_sprites->Draw(m_FirstRenderPass->getShaderResourceView(), m_CameraViewRect);
	m_sprites->End();

    // Show the new frame.
    m_deviceResources->Present();
}


void Game::PostProcess()
{
    auto context = m_deviceResources->GetD3DDeviceContext();
    //auto renderTargetView = m_offscreenTexture->getRenderTargetView();
    //auto depthTargetView = m_deviceResources->GetDepthStencilView();

   // m_offscreenTexture->setRenderTarget(context);


    ID3D11ShaderResourceView* null[] = { nullptr, nullptr };

    if (g_Bloom == None)
    {
        // Pass-through test
        context->CopyResource(m_deviceResources->GetRenderTarget(),
            m_offscreenTexture->getRenderTarget());
    }
    else
    {
        // scene -> RT1 (downsample)
        auto rt1RT = m_renderTarget1->getRenderTargetView();
        context->OMSetRenderTargets(1, &rt1RT, nullptr);
        m_sprites->Begin(SpriteSortMode_Immediate,
            nullptr, nullptr, nullptr, nullptr,
            [=]() {
                context->PSSetConstantBuffers(0, 1, m_bloomParams.GetAddressOf());
                context->PSSetShader(m_bloomExtractPS.Get(), nullptr, 0);
            });
        auto rtSRV = m_FirstRenderPass->getShaderResourceView();
        m_sprites->Draw(rtSRV, T_BloomSize);
        m_sprites->End();

        // RT1 -> RT2 (blur horizontal)
        auto rt2RT = m_renderTarget2->getRenderTargetView();
        context->OMSetRenderTargets(1, &rt2RT, nullptr);
        m_sprites->Begin(SpriteSortMode_Immediate,
            nullptr, nullptr, nullptr, nullptr,
            [=]() {
                context->PSSetShader(m_gaussianBlurPS.Get(), nullptr, 0);
                context->PSSetConstantBuffers(0, 1,
                    m_blurParamsWidth.GetAddressOf());
            });
        auto rt1SRV = m_renderTarget1->getShaderResourceView();
        m_sprites->Draw(rt1SRV, T_BloomSize);
        m_sprites->End();

        context->PSSetShaderResources(0, 2, null);

        // RT2 -> RT1 (blur vertical)
        context->OMSetRenderTargets(1, &rt1RT, nullptr);
        m_sprites->Begin(SpriteSortMode_Immediate,
            nullptr, nullptr, nullptr, nullptr,
            [=]() {
                context->PSSetShader(m_gaussianBlurPS.Get(), nullptr, 0);
                context->PSSetConstantBuffers(0, 1,
                    m_blurParamsHeight.GetAddressOf());
            });
        auto rt2SRV = m_renderTarget2->getShaderResourceView();
        m_sprites->Draw(rt2SRV, T_BloomSize);
        m_sprites->End();

        // RT1 + scene
        auto renderTarget = m_deviceResources->GetRenderTargetView();
        context->OMSetRenderTargets(1, &renderTarget, nullptr);
        m_sprites->Begin(SpriteSortMode_Immediate,
            nullptr, nullptr, nullptr, nullptr,
            [=]() {
                context->PSSetShader(m_bloomCombinePS.Get(), nullptr, 0);
                context->PSSetShaderResources(1, 1, &rt1SRV);
                context->PSSetConstantBuffers(0, 1, m_bloomParams.GetAddressOf());
            });
        m_sprites->Draw(rtSRV, T_FullScreen);
        m_sprites->End();
    }
    context->PSSetShaderResources(0, 2, null);
}


void Game::GenerateVolumetricFogTexture(ID3D11ShaderResourceView** fogTexture)
{
    auto device = m_deviceResources->GetD3DDevice();
    auto context = m_deviceResources->GetD3DDeviceContext();
    const int width = 128;
    const int height = 128;

    // Create a 3D texture for volumetric fog
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture3D;
    CD3D11_TEXTURE2D_DESC textureDesc(
        DXGI_FORMAT_R8G8B8A8_UNORM,
        width,
        height,
        1,
        D3D11_BIND_SHADER_RESOURCE
    );
    device->CreateTexture2D(&textureDesc, nullptr, texture3D.GetAddressOf());

    // Initialize texture data
    std::vector<Color> texels(width * height);
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                // Calculate Perlin noise value at current position
                float noise = ClassicNoise::noise(x / float(width), y / float(height), 1);

                // Set fog density based on noise value (adjust parameters as needed)
                float density = noise * 0.5f + 0.5f; // Map noise to [0, 1] range

                // Set fog color (adjust as needed)
                Color color(0.8f, 0.8f, 0.8f, density);

                // Write color to texture
                texels[x + y * width] = color;
            }
        }

    // Upload texture data to GPU
    context->UpdateSubresource(texture3D.Get(), 0, nullptr, texels.data(), width * sizeof(Color), 0);

    // Create shader resource view for the texture
    CD3D11_SHADER_RESOURCE_VIEW_DESC srvDesc(D3D11_SRV_DIMENSION_TEXTURE2D, textureDesc.Format);
    device->CreateShaderResourceView(texture3D.Get(), &srvDesc, fogTexture);
}


void Game::RenderTexturePass1()
{
	auto context = m_deviceResources->GetD3DDeviceContext();
	auto renderTargetView = m_deviceResources->GetRenderTargetView();
	auto depthTargetView = m_deviceResources->GetDepthStencilView();
	// Set the render target to be the render to texture.
	m_FirstRenderPass->setRenderTarget(context);
	// Clear the render to texture.
	m_FirstRenderPass->clearRenderTarget(context, 0.0f, 0.0f, 1.0f, 1.0f);

	// Turn our shaders on,  set parameters
	//m_BasicShaderPair.EnableShader(context);
	//m_BasicShaderPair.SetShaderParameters(context, &m_world, &m_view, &m_projection, &m_Light, m_texture1.Get());
	//m_HorizontalBlur.EnableShader(context);
	//m_HorizontalBlur.SetShaderParametersBlur(context, &m_world, &m_view, &m_projection, &m_Light, m_texture1.Get(), 800);

	//render our model	
	//m_BasicModel.Render(context);
	
	m_world = SimpleMath::Matrix::Identity; //set world back to identity
	SimpleMath::Matrix newPosition3 = SimpleMath::Matrix::CreateTranslation(0.0f, -0.6f, 0.0f);
	SimpleMath::Matrix newScale = SimpleMath::Matrix::CreateScale(0.1);
	SimpleMath::Matrix newRotation = SimpleMath::Matrix::CreateRotationX(190);//scale the terrain down a little. 
	m_world = m_world * newScale * newPosition3 * newRotation;

	//setup and draw cube
	m_BasicShaderPair1.EnableShader(context);
	m_BasicShaderPair1.SetShaderParameters(context, &m_world, &m_view, &m_projection, &m_Light, m_texture1.Get());
	m_Terrain.Render(context);
	//m_FullScreen.Render(context);

	// Reset the render target back to the original back buffer and not the render to texture anymore.	
	context->OMSetRenderTargets(1, &renderTargetView, depthTargetView);
}

// Helper method to clear the back buffers.
void Game::Clear()
{
    m_deviceResources->PIXBeginEvent(L"Clear");

    // Clear the views.
    auto context = m_deviceResources->GetD3DDeviceContext();
    auto renderTarget = m_deviceResources->GetRenderTargetView();
    auto depthStencil = m_deviceResources->GetDepthStencilView();

    context->ClearRenderTargetView(renderTarget, Colors::CornflowerBlue);
    context->ClearDepthStencilView(depthStencil, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    context->OMSetRenderTargets(1, &renderTarget, depthStencil);

    // Set the viewport.
    auto viewport = m_deviceResources->GetScreenViewport();
    context->RSSetViewports(1, &viewport);

    m_deviceResources->PIXEndEvent();
}

#pragma endregion

#pragma region Message Handlers
// Message handlers
void Game::OnActivated()
{
}

void Game::OnDeactivated()
{
}

void Game::OnSuspending()
{
#ifdef DXTK_AUDIO
    m_audEngine->Suspend();
#endif
}

void Game::OnResuming()
{
    m_timer.ResetElapsedTime();

#ifdef DXTK_AUDIO
    m_audEngine->Resume();
#endif
}

void Game::OnWindowMoved()
{
    auto r = m_deviceResources->GetOutputSize();
    m_deviceResources->WindowSizeChanged(r.right, r.bottom);
}

void Game::OnWindowSizeChanged(int width, int height)
{
    if (!m_deviceResources->WindowSizeChanged(width, height))
        return;

    CreateWindowSizeDependentResources();
}

#ifdef DXTK_AUDIO
void Game::NewAudioDevice()
{
    if (m_audEngine && !m_audEngine->IsAudioDevicePresent())
    {
        // Setup a retry in 1 second
        m_audioTimerAcc = 1.f;
        m_retryDefault = true;
    }
}
#endif

// Properties
void Game::GetDefaultSize(int& width, int& height) const
{
    width = 800;
    height = 600;
}
#pragma endregion

#pragma region Direct3D Resources
// These are the resources that depend on the device.
void Game::CreateDeviceDependentResources()
{
    auto context = m_deviceResources->GetD3DDeviceContext();
    auto device = m_deviceResources->GetD3DDevice();

    m_states = std::make_unique<CommonStates>(device);
    m_fxFactory = std::make_unique<EffectFactory>(device);
    m_sprites = std::make_unique<SpriteBatch>(context);
    m_font = std::make_unique<SpriteFont>(device, L"SegoeUI_18.spritefont");
	m_batch = std::make_unique<PrimitiveBatch<VertexPositionColor>>(context);
	m_trialPostProcess = std::make_unique<BasicPostProcess>(device);


//trying post process again
	{
		DX::ThrowIfFailed(CreateDDSTextureFromFile(device,
			L"darksky.dds", nullptr,
			m_background.ReleaseAndGetAddressOf()));
	}
    auto blob = DX::ReadData(L"BloomExtract.cso");
    DX::ThrowIfFailed(device->CreatePixelShader(blob.data(), blob.size(),
        nullptr, m_bloomExtractPS.ReleaseAndGetAddressOf()));

    blob = DX::ReadData(L"BloomCombine.cso");
    DX::ThrowIfFailed(device->CreatePixelShader(blob.data(), blob.size(),
        nullptr, m_bloomCombinePS.ReleaseAndGetAddressOf()));

    blob = DX::ReadData(L"GaussianBlur.cso");
    DX::ThrowIfFailed(device->CreatePixelShader(blob.data(), blob.size(),
        nullptr, m_gaussianBlurPS.ReleaseAndGetAddressOf()));

    {
        CD3D11_BUFFER_DESC cbDesc(sizeof(VS_BLOOM_PARAMETERS),
            D3D11_BIND_CONSTANT_BUFFER);
        D3D11_SUBRESOURCE_DATA initData = { &g_BloomPresets[g_Bloom], 0, 0 };
        DX::ThrowIfFailed(device->CreateBuffer(&cbDesc, &initData,
            m_bloomParams.ReleaseAndGetAddressOf()));
    }

    {
        CD3D11_BUFFER_DESC cbDesc(sizeof(VS_BLUR_PARAMETERS),
            D3D11_BIND_CONSTANT_BUFFER);
        DX::ThrowIfFailed(device->CreateBuffer(&cbDesc, nullptr,
            m_blurParamsWidth.ReleaseAndGetAddressOf()));
        DX::ThrowIfFailed(device->CreateBuffer(&cbDesc, nullptr,
            m_blurParamsHeight.ReleaseAndGetAddressOf()));
    }


	//setup our terrain
	m_Terrain.Initialize(device, 64, 64);
	m_FullScreen.InitializeBuffersForBlur(device, 1024, 800);

	//setup our test model
	m_BasicModel.InitializeSphere(device);
	m_BasicModel2.InitializeModel(device,"drone.obj");
	m_BasicModel3.InitializeBox(device, 10.0f, 0.1f, 10.0f);	//box includes dimensions

	//load and set up our Vertex and Pixel Shaders
	m_BasicShaderPair.InitStandard(device, L"light_vs.cso", L"light_ps.cso");
	m_BasicShaderPair1.InitStandard(device, L"light_vs.cso", L"light_ps.cso");
	m_HorizontalBlur.InitStandard(device, L"BlurVS.cso", L"BlurPS.cso");

	//load Textures
	CreateDDSTextureFromFile(device, L"seafloor.dds",		nullptr,	m_texture1.ReleaseAndGetAddressOf());
	CreateDDSTextureFromFile(device, L"EvilDrone_Diff.dds", nullptr,	m_texture2.ReleaseAndGetAddressOf());
    CreateDDSTextureFromFile(device, L"cloud.dds",          nullptr,    m_Cloud.ReleaseAndGetAddressOf());

	//Initialise Render to texture
	m_FirstRenderPass = new RenderTexture(device, 800, 600, 1, 2);	//for our rendering, We dont use the last two properties. but.  they cant be zero and they cant be the same. 
	//m_PostProcess = new RenderTexture(device, 800, 600, 1, 2);
    m_offscreenTexture = new RenderTexture(device, 800, 600, 1, 2);
    m_renderTarget1 = new RenderTexture(device, 400, 300, 1, 2);
    m_renderTarget2 = new RenderTexture(device, 400, 300, 1, 2);
}

// Allocate all memory resources that change on a window SizeChanged event.
void Game::CreateWindowSizeDependentResources()
{
    auto context = m_deviceResources->GetD3DDeviceContext();
    auto size = m_deviceResources->GetOutputSize();
    float aspectRatio = float(size.right) / float(size.bottom);
    float fovAngleY = 70.0f * XM_PI / 180.0f;
    T_FullScreen = size;
    T_BloomSize = { 0, 0, size.right / 2, size.bottom / 2 };


    VS_BLUR_PARAMETERS blurData = {};   
    blurData.SetBlurEffectParameters(1.f / (float(size.right) / 2), 0,
        g_BloomPresets[g_Bloom]);
    context->UpdateSubresource(m_blurParamsWidth.Get(), 0, nullptr,
        &blurData, sizeof(VS_BLUR_PARAMETERS), 0);

    blurData.SetBlurEffectParameters(0, 1.f / (float(size.bottom) / 2),
        g_BloomPresets[g_Bloom]);
    context->UpdateSubresource(m_blurParamsHeight.Get(), 0, nullptr,
        &blurData, sizeof(VS_BLUR_PARAMETERS), 0);


    // This is a simple example of change that can be made when the app is in
    // portrait or snapped view.
    if (aspectRatio < 1.0f)
    {
        fovAngleY *= 2.0f;
    }

    // This sample makes use of a right-handed coordinate system using row-major matrices.
    m_projection = Matrix::CreatePerspectiveFieldOfView(
        fovAngleY,
        aspectRatio,
        0.01f,
        100.0f
    );
}

void Game::SetupGUI()
{
	auto device = m_deviceResources->GetD3DDevice();
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	ImGui::Begin("Parameters");
		ImGui::SliderFloat("Wave Amplitude",	m_Terrain.GetAmplitude(), 0.0f, 10.0f);
		//ImGui::SliderFloat("Wavelength",		m_Terrain.GetWavelength(), 0.0f, 1.0f);
		ImGui::BeginChild("Noise Types");
		{
			if(ImGui::Button("Perlin Noise"))
				m_Terrain.GeneratePerlinNoise(device);
			if (ImGui::Button("Simplex Noise"))
				m_Terrain.GenerateSimplexNoise (device);
			if (ImGui::Button("GenerateWaves"))
				m_Terrain.Update(device); 
		}
		ImGui::EndChild();
	ImGui::End();
}




void Game::OnDeviceLost()
{
    m_states.reset();
    m_fxFactory.reset();
    m_sprites.reset();
    m_font.reset();
	m_batch.reset();
	m_testmodel.reset();
    m_batchInputLayout.Reset();

    m_bloomExtractPS.Reset();
    m_bloomCombinePS.Reset();
    m_gaussianBlurPS.Reset();

    m_bloomParams.Reset();
    m_blurParamsWidth.Reset();
    m_blurParamsHeight.Reset();
}

void Game::OnDeviceRestored()
{
    CreateDeviceDependentResources();
    CreateWindowSizeDependentResources();
}
#pragma endregion
