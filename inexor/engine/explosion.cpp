#include <SDL_opengl.h>                  // for glDepthFunc, GLushort, GL_UN...
#include <math.h>                        // for cos, sin, M_PI
#include <string.h>                      // for memmove
#include <algorithm>                     // for max, min

#include "SDL_opengl.h"                  // for GL_STATIC_DRAW, GL_ARRAY_BUFFER
#include "inexor/engine/depthfx.hpp"     // for depthfxtexture, depthfxtex
#include "inexor/engine/explosion.hpp"
#include "inexor/engine/glare.hpp"       // for glaring
#include "inexor/engine/glemu.hpp"       // for bindebo, bindvbo, clearebo
#include "inexor/engine/glexts.hpp"      // for glBufferData_, glDeleteBuffers_
#include "inexor/engine/octree.hpp"      // for glde
#include "inexor/engine/rendergl.hpp"    // for camera1, camprojmatrix, camr...
#include "inexor/engine/renderva.hpp"    // for isfoggedsphere
#include "inexor/engine/shader.hpp"      // for LOCALPARAM, SETSHADER, Shader
#include "inexor/engine/water.hpp"       // for reflecting, refracting, refl...
#include "inexor/network/SharedVar.hpp"  // for SharedVar
#include "inexor/physics/physics.hpp"    // for collide
#include "inexor/shared/cube_loops.hpp"  // for i, k, loopi, j, loopk, loopj
#include "inexor/shared/cube_tools.hpp"  // for DELETEA
#include "inexor/shared/cube_types.hpp"  // for ushort, SQRT3, RAD
#include "inexor/shared/ents.hpp"        // for physent, extentity, ::ENT_CA...
#include "inexor/shared/geom.hpp"        // for vec, vec::(anonymous union):...
#include "inexor/util/legacy_time.hpp"   // for lastmillis

namespace sphere
{

    struct vert
    {
        vec pos;
        ushort s, t;
    } *verts = nullptr;
    GLushort *indices = nullptr;
    int numverts = 0, numindices = 0;
    GLuint vbuf = 0, ebuf = 0;

    void init(int slices, int stacks)
    {
        numverts = (stacks+1)*(slices+1);
        verts = new vert[numverts];
        float ds = 1.0f/slices, dt = 1.0f/stacks, t = 1.0f;
        loopi(stacks+1)
        {
            float rho = M_PI*(1-t), s = 0.0f, sinrho = i && i < stacks ? sin(rho) : 0, cosrho = !i ? 1 : (i < stacks ? cos(rho) : -1);
            loopj(slices+1)
            {
                float theta = j==slices ? 0 : 2*M_PI*s;
                vert &v = verts[i*(slices+1) + j];
                v.pos = vec(-sin(theta)*sinrho, cos(theta)*sinrho, cosrho);
                v.s = ushort(s*0xFFFF);
                v.t = ushort(t*0xFFFF);
                s += ds;
            }
            t -= dt;
        }

        numindices = (stacks-1)*slices*3*2;
        indices = new ushort[numindices];
        GLushort *curindex = indices;
        loopi(stacks)
        {
            loopk(slices)
            {
                int j = i%2 ? slices-k-1 : k;
                if(i)
                {
                    *curindex++ = i*(slices+1)+j;
                    *curindex++ = (i+1)*(slices+1)+j;
                    *curindex++ = i*(slices+1)+j+1;
                }
                if(i+1 < stacks)
                {
                    *curindex++ = i*(slices+1)+j+1;
                    *curindex++ = (i+1)*(slices+1)+j;
                    *curindex++ = (i+1)*(slices+1)+j+1;
                }
            }
        }

        if(!vbuf) glGenBuffers_(1, &vbuf);
        gle::bindvbo(vbuf);
        glBufferData_(GL_ARRAY_BUFFER, numverts*sizeof(vert), verts, GL_STATIC_DRAW);
        DELETEA(verts);

        if(!ebuf) glGenBuffers_(1, &ebuf);
        gle::bindebo(ebuf);
        glBufferData_(GL_ELEMENT_ARRAY_BUFFER, numindices*sizeof(GLushort), indices, GL_STATIC_DRAW);
        DELETEA(indices);
    }

    void enable()
    {
        if(!vbuf) init(12, 6);

        gle::bindvbo(vbuf);
        gle::bindebo(ebuf);
   
        gle::vertexpointer(sizeof(vert), &verts->pos);
        gle::texcoord0pointer(sizeof(vert), &verts->s, GL_UNSIGNED_SHORT, 2, GL_TRUE);
        gle::enablevertex();
        gle::enabletexcoord0();
    }

    void draw()
    {
        glDrawRangeElements_(GL_TRIANGLES, 0, numverts-1, numindices, GL_UNSIGNED_SHORT, indices);
        xtraverts += numindices;
        glde++;
    }

    void disable()
    {
        gle::disablevertex();
        gle::disabletexcoord0();

        gle::clearvbo();
        gle::clearebo();
    }

    void cleanup()
    {
        if(vbuf) { glDeleteBuffers_(1, &vbuf); vbuf = 0; }
        if(ebuf) { glDeleteBuffers_(1, &ebuf); ebuf = 0; }
    }
}

static const float WOBBLE = 1.25f;

void fireballrenderer::startrender()
{
    if(glaring) SETSHADER(explosionglare);
    else if(!reflecting && !refracting && depthfx && depthfxtex.rendertex && numdepthfxranges>0)
    {
        if(!depthfxtex.highprecision()) SETSHADER(explosionsoft8);
        else SETSHADER(explosionsoft);
    }
    else SETSHADER(explosion);

    sphere::enable();
}

void fireballrenderer::endrender()
{
    sphere::disable();
}

void fireballrenderer::cleanup()
{
    sphere::cleanup();
}

int fireballrenderer::finddepthfxranges(void **owners, float *ranges, int numranges, int maxranges, vec &bbmin, vec &bbmax)
{
    static struct fireballent : physent
    {
        fireballent()
        {
            type = ENT_CAMERA;
        }
    } e;

    for(listparticle *p = list; p; p = p->next)
    {
        int ts = p->fade <= 5 ? 1 : lastmillis-p->millis;
        float pmax = p->val,
              size = p->fade ? float(ts)/p->fade : 1,
              psize = (p->size + pmax * size)*WOBBLE;
        if(2*(p->size + pmax)*WOBBLE < depthfxblend ||
           (!depthfxtex.highprecision() && !depthfxtex.emulatehighprecision() && psize > depthfxscale - depthfxbias) ||
           isfoggedsphere(psize, p->o)) continue;

        e.o = p->o;
        e.radius = e.xradius = e.yradius = e.eyeheight = e.aboveeye = psize;
        if(!::collide(&e, vec(0, 0, 0), 0, false)) continue;

        if(depthfxscissor==2 && !depthfxtex.addscissorbox(p->o, psize)) continue;

        vec dir = camera1->o;
        dir.sub(p->o);
        float dist = dir.magnitude();
        dir.mul(psize/dist).add(p->o);
        float depth = depthfxtex.eyedepth(dir);

        loopk(3)
        {
            bbmin[k] = std::min(bbmin[k], p->o[k] - psize);
            bbmax[k] = std::max(bbmax[k], p->o[k] + psize);
        }

        int pos = numranges;
        loopi(numranges) if(depth < ranges[i]) { pos = i; break; }
        if(pos >= maxranges) continue;

        if(numranges > pos)
        {
            int moved = std::min(numranges-pos, maxranges-(pos+1));
            memmove(&ranges[pos+1], &ranges[pos], moved*sizeof(float));
            memmove(&owners[pos+1], &owners[pos], moved*sizeof(void *));
        }
        if(numranges < maxranges) numranges++;

        ranges[pos] = depth;
        owners[pos] = p;
    }

    return numranges;
}

void fireballrenderer::seedemitter(particleemitter &pe, const vec &o, const vec &d, int fade, float size, int gravity)
{
    pe.maxfade = std::max(pe.maxfade, fade);
    pe.extendbb(o, (size+1+pe.ent->attr2)*WOBBLE);
}

void fireballrenderer::renderpart(listparticle *p, const vec &o, const vec &d, int blend, int ts)
{
    float pmax = p->val,
          size = p->fade ? float(ts)/p->fade : 1,
          psize = p->size + pmax * size;

    if(isfoggedsphere(psize*WOBBLE, p->o)) return;

    vec dir = vec(o).sub(camera1->o), s, t;
    float dist = dir.magnitude();
    bool inside = dist <= psize*WOBBLE;
    if(inside)
    {
        s = camright;
        t = camup;
    }
    else
    {
        if(reflecting) { dir.z = o.z - reflectz; dist = dir.magnitude(); }
        float mag2 = dir.magnitude2();
        dir.x /= mag2;
        dir.y /= mag2;
        dir.z /= dist;
        s = vec(dir.y, -dir.x, 0);
        t = vec(dir.x*dir.z, dir.y*dir.z, -mag2/dist);
    }

    matrix3 rot(lastmillis/1000.0f*143*RAD, vec(1/SQRT3, 1/SQRT3, 1/SQRT3));
    LOCALPARAM(texgenS, rot.transposedtransform(s));
    LOCALPARAM(texgenT, rot.transposedtransform(t));

    matrix4 m(rot, o);
    m.scale(psize, psize, inside ? -psize : psize);
    m.mul(camprojmatrix, m);
    LOCALPARAM(explosionmatrix, m);

    LOCALPARAM(center, o);
    LOCALPARAMF(millis, lastmillis/1000.0f);
    LOCALPARAMF(blendparams, inside ? 0.5f : 4, inside ? 0.25f : 0);
    binddepthfxparams(depthfxblend, inside ? blend/(2*255.0f) : 0, 2*(p->size + pmax)*WOBBLE >= depthfxblend, p);

    int passes = !reflecting && !refracting && inside ? 2 : 1;
    loopi(passes)
    {
        gle::color(p->color, i ? blend/2 : blend);
        if(i) glDepthFunc(GL_GEQUAL);
        sphere::draw();
        if(i) glDepthFunc(GL_LESS);
    }
}
fireballrenderer fireballs("particle/explosion.png"), bluefireballs("particle/plasma.png");

