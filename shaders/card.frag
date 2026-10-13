#version 440
layout(location=0) in vec2 qt_TexCoord0;
layout(location=0) out vec4 fragColor;
layout(std140,binding=0) uniform buf {
 mat4 qt_Matrix;
 float qt_Opacity;
 vec2 itemSize;
 float skew;
 float dimming;
 float focused;
 vec4 accent;
};
layout(binding=1) uniform sampler2D source;
void main(){
 vec2 p=qt_TexCoord0*itemSize;
 float slope=skew/itemSize.y;
 float left=skew*(1.0-qt_TexCoord0.y);
 float right=itemSize.x-skew*qt_TexCoord0.y;
 float side=min(p.x-left,right-p.x)/sqrt(1.0+slope*slope);
 float edge=min(side,min(p.y,itemSize.y-p.y));
 float mask=smoothstep(0.0,1.25,edge);
 float border=(1.0-smoothstep(1.0,2.0,edge))*mix(0.22,0.8,focused);
 vec4 c=texture(source,qt_TexCoord0);
 c.rgb*=1.0-dimming;
 float shade=smoothstep(0.46,1.0,qt_TexCoord0.y)*0.26;
 c.rgb*=1.0-shade;
 c=mix(c,accent,border);
 fragColor=c*mask*qt_Opacity;
}
