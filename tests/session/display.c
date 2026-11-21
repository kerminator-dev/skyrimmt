/* DXMT regression: render a gradient with a viewport derived from GetClientRect,
 * like Skyrim's scene. Read pixels to catch cropping, not just buffer dimensions. */
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <stdio.h>
#include <stdlib.h>
#define REQUIRE(x) do { HRESULT r=(x); if(FAILED(r)) { printf("FAIL %s: %lx\n",#x,r); return 1; } } while(0)
static const char *shader =
    "struct V { float4 pos:SV_POSITION; float2 uv:TEXCOORD0; };"
    "V vs(uint id:SV_VertexID) { V o; o.uv=float2((id<<1)&2,id&2);"
    "o.pos=float4(o.uv*float2(2,-2)+float2(-1,1),0,1); return o; }"
    "float4 ps(V i):SV_TARGET { return float4(i.uv,0,1); }";
int main(void) {
    int percent=getenv("SKYRIMMT_TEST_PERCENT") ? atoi(getenv("SKYRIMMT_TEST_PERCENT")) : 50;
    if(percent!=50 && percent!=75 && percent!=100) return 1;
    UINT width=(GetSystemMetrics(SM_CXSCREEN)*percent/200)*2;
    UINT height=(GetSystemMetrics(SM_CYSCREEN)*percent/200)*2;
    WNDCLASSA wc={0}; wc.lpfnWndProc=DefWindowProcA; wc.hInstance=GetModuleHandle(NULL); wc.lpszClassName="SkyrimMTDisplayTest";
    RegisterClassA(&wc);
    HWND wnd=CreateWindowA(wc.lpszClassName,"SkyrimMT display test",WS_POPUP|WS_VISIBLE,0,0,width,height,NULL,NULL,wc.hInstance,NULL);
    DXGI_SWAP_CHAIN_DESC desc={0}; desc.BufferDesc.Width=width; desc.BufferDesc.Height=height;
    desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count=1;
    desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount=2;
    desc.OutputWindow=wnd; desc.Windowed=TRUE; desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    IDXGISwapChain *sc=NULL; ID3D11Device *dev=NULL; ID3D11DeviceContext *ctx=NULL;
    REQUIRE(D3D11CreateDeviceAndSwapChain(NULL,D3D_DRIVER_TYPE_HARDWARE,NULL,0,NULL,0,D3D11_SDK_VERSION,&desc,&sc,&dev,NULL,&ctx));
    ID3DBlob *vc=NULL,*pc=NULL;
    REQUIRE(D3DCompile(shader,strlen(shader),NULL,NULL,NULL,"vs","vs_5_0",0,0,&vc,NULL));
    REQUIRE(D3DCompile(shader,strlen(shader),NULL,NULL,NULL,"ps","ps_5_0",0,0,&pc,NULL));
    ID3D11VertexShader *vs=NULL; ID3D11PixelShader *ps=NULL;
    REQUIRE(ID3D11Device_CreateVertexShader(dev,ID3D10Blob_GetBufferPointer(vc),ID3D10Blob_GetBufferSize(vc),NULL,&vs));
    REQUIRE(ID3D11Device_CreatePixelShader(dev,ID3D10Blob_GetBufferPointer(pc),ID3D10Blob_GetBufferSize(pc),NULL,&ps));
    ID3D10Blob_Release(vc); ID3D10Blob_Release(pc);
    ID3D11DeviceContext_VSSetShader(ctx,vs,NULL,0); ID3D11DeviceContext_PSSetShader(ctx,ps,NULL,0);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    unsigned char red=0,green=0;
    for(int i=0;i<5;i++) {
        MSG msg; while(PeekMessage(&msg,NULL,0,0,PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); }
        RECT logical; GetClientRect(wnd,&logical);
        D3D11_VIEWPORT viewport={0,0,(FLOAT)logical.right,(FLOAT)logical.bottom,0,1};
        ID3D11DeviceContext_RSSetViewports(ctx,1,&viewport);
        ID3D11Texture2D *buffer=NULL; ID3D11RenderTargetView *view=NULL;
        REQUIRE(IDXGISwapChain_GetBuffer(sc,0,&IID_ID3D11Texture2D,(void**)&buffer));
        REQUIRE(ID3D11Device_CreateRenderTargetView(dev,(ID3D11Resource*)buffer,NULL,&view));
        FLOAT black[4]={0,0,0,1}; ID3D11DeviceContext_ClearRenderTargetView(ctx,view,black);
        ID3D11DeviceContext_OMSetRenderTargets(ctx,1,&view,NULL); ID3D11DeviceContext_Draw(ctx,3,0);
        if(i==4) {
            D3D11_TEXTURE2D_DESC td; ID3D11Texture2D_GetDesc(buffer,&td);
            td.Usage=D3D11_USAGE_STAGING; td.BindFlags=0; td.CPUAccessFlags=D3D11_CPU_ACCESS_READ; td.MiscFlags=0;
            ID3D11Texture2D *readback=NULL; REQUIRE(ID3D11Device_CreateTexture2D(dev,&td,NULL,&readback));
            ID3D11DeviceContext_CopyResource(ctx,(ID3D11Resource*)readback,(ID3D11Resource*)buffer);
            D3D11_MAPPED_SUBRESOURCE mapped;
            REQUIRE(ID3D11DeviceContext_Map(ctx,(ID3D11Resource*)readback,0,D3D11_MAP_READ,0,&mapped));
            unsigned char *pixel=(unsigned char*)mapped.pData+(height*9/10)*mapped.RowPitch+(width*9/10)*4;
            red=pixel[0]; green=pixel[1];
            ID3D11DeviceContext_Unmap(ctx,(ID3D11Resource*)readback,0); ID3D11Texture2D_Release(readback);
        }
        ID3D11DeviceContext_OMSetRenderTargets(ctx,0,NULL,NULL);
        ID3D11RenderTargetView_Release(view); ID3D11Texture2D_Release(buffer);
        REQUIRE(IDXGISwapChain_Present(sc,1,0)); Sleep(50);
    }
    RECT logical,physical; GetClientRect(wnd,&logical);
    BOOL (WINAPI *systemRect)(HWND,LPRECT)=(void*)GetProcAddress(GetModuleHandleA("user32.dll"),"GetClientRect");
    systemRect(wnd,&physical); /* bypass game import, as Wine/DXMT must */
    MONITORINFO mon={.cbSize=sizeof(MONITORINFO)}; GetMonitorInfo(MonitorFromWindow(wnd,MONITOR_DEFAULTTONEAREST),&mon);
    int stretch=getenv("SKYRIMMT_STRETCH") && !strcmp(getenv("SKYRIMMT_STRETCH"),"1");
    int expectedW=stretch ? mon.rcMonitor.right-mon.rcMonitor.left : width;
    int expectedH=stretch ? mon.rcMonitor.bottom-mon.rcMonitor.top : height;
    REQUIRE(IDXGISwapChain_ResizeBuffers(sc,0,0,0,DXGI_FORMAT_UNKNOWN,0));
    IDXGISwapChain_GetDesc(sc,&desc);
    int ok=physical.right==expectedW && physical.bottom==expectedH && logical.right==width && logical.bottom==height
        && desc.BufferDesc.Width==width && desc.BufferDesc.Height==height && red>210 && green>210;
    printf("%s stretch=%d scale=%d%% physical=%ldx%ld logical=%ldx%ld buffer=%ux%u corner=%u,%u (expected >210,>210)\n",
        ok?"PASS":"FAIL",stretch,percent,physical.right,physical.bottom,logical.right,logical.bottom,desc.BufferDesc.Width,desc.BufferDesc.Height,red,green);
    ID3D11VertexShader_Release(vs); ID3D11PixelShader_Release(ps);
    IDXGISwapChain_Release(sc); ID3D11DeviceContext_Release(ctx); ID3D11Device_Release(dev); DestroyWindow(wnd);
    return ok?0:3;
}
