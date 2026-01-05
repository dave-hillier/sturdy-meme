/*
 * lime-init/08-init-final.js
 * Part 8/8: Initialization and Bootstrap
 * Contains: OpenFL events, filters, media, initialization
 */
                h += H.string(d.toGLSL() + " = exp2(" + g.toGLSL() + "); // exp");
                f.addDR(d, fa.VECTOR_4);
                f.addSR(g, fa.VECTOR_4);
                break;
            case 14:
                h += H.string(d.toGLSL() + " = normalize(" + g.toGLSL() + "); // normalize");
                f.addDR(d, fa.VECTOR_4);
                f.addSR(g, fa.VECTOR_4);
                break;
            case 15:
                h += H.string(d.toGLSL() + " = sin(" + g.toGLSL() + "); // sin");
                f.addDR(d, fa.VECTOR_4);
                f.addSR(g, fa.VECTOR_4);
                break;
            case 16:
                h += H.string(d.toGLSL() + " = cos(" + g.toGLSL() + "); // cos");
                f.addDR(d, fa.VECTOR_4);
                f.addSR(g,
                    fa.VECTOR_4);
                break;
            case 17:
                g.sourceMask = q.sourceMask = 7;
                h += H.string(d.toGLSL() + " = cross(vec3(" + g.toGLSL() + "), vec3(" + q.toGLSL() + ")); // crs");
                f.addDR(d, fa.VECTOR_4);
                f.addSR(g, fa.VECTOR_4);
                f.addSR(q, fa.VECTOR_4);
                break;
            case 18:
                g.sourceMask = q.sourceMask = 7;
                h += H.string(d.toGLSL() + " = vec4(dot(vec3(" + g.toGLSL() + "), vec3(" + q.toGLSL() + ")))" + d.getWriteMask() + "; // dp3");
                f.addDR(d, fa.VECTOR_4);
                f.addSR(g, fa.VECTOR_4);
                f.addSR(q, fa.VECTOR_4);
                break;
            case 19:
                g.sourceMask = q.sourceMask = 15;
                h += H.string(d.toGLSL() + " = vec4(dot(vec4(" +
                    g.toGLSL() + "), vec4(" + q.toGLSL() + ")))" + d.getWriteMask() + "; // dp4");
                f.addDR(d, fa.VECTOR_4);
                f.addSR(g, fa.VECTOR_4);
                f.addSR(q, fa.VECTOR_4);
                break;
            case 20:
                h += H.string(d.toGLSL() + " = abs(" + g.toGLSL() + "); // abs");
                f.addDR(d, fa.VECTOR_4);
                f.addSR(g, fa.VECTOR_4);
                break;
            case 21:
                h += H.string(d.toGLSL() + " = -" + g.toGLSL() + "; // neg");
                f.addDR(d, fa.VECTOR_4);
                f.addSR(g, fa.VECTOR_4);
                break;
            case 22:
                h += H.string(d.toGLSL() + " = clamp(" + g.toGLSL() + ", 0.0, 1.0); // saturate");
                f.addDR(d, fa.VECTOR_4);
                f.addSR(g, fa.VECTOR_4);
                break;
            case 23:
                k = f.getRegisterUsage(q);
                k != fa.VECTOR_4 && k != fa.VECTOR_4_ARRAY ? (h += H.string(d.toGLSL() + " = " + g.toGLSL() + " * mat3(" + q.toGLSL(!1) + "); // m33"), f.addDR(d, fa.VECTOR_4), f.addSR(g, fa.VECTOR_4), f.addSR(q, fa.MATRIX_4_4)) : (g.sourceMask = q.sourceMask = 7, h += H.string(d.toGLSL() + " = vec3(dot(" + g.toGLSL(!0) + "," + q.toGLSL(!0, 0) + "), dot(" + g.toGLSL(!0) + "," + q.toGLSL(!0, 1) + "),dot(" + g.toGLSL(!0) + "," + q.toGLSL(!0, 2) + ")); // m33"), f.addDR(d, fa.VECTOR_4), f.addSR(g, fa.VECTOR_4), f.addSR(q, fa.VECTOR_4, 0), f.addSR(q,
                    fa.VECTOR_4, 1), f.addSR(q, fa.VECTOR_4, 2));
                break;
            case 24:
                k = f.getRegisterUsage(q);
                k != fa.VECTOR_4 && k != fa.VECTOR_4_ARRAY ? (h += H.string(d.toGLSL() + " = " + g.toGLSL() + " * " + q.toGLSL(!1) + "; // m44"), f.addDR(d, fa.VECTOR_4), f.addSR(g, fa.VECTOR_4), f.addSR(q, fa.MATRIX_4_4)) : (g.sourceMask = q.sourceMask = 15, h += H.string(d.toGLSL() + " = vec4(dot(" + g.toGLSL(!0) + "," + q.toGLSL(!0, 0) + "), dot(" + g.toGLSL(!0) + "," + q.toGLSL(!0, 1) + "), dot(" + g.toGLSL(!0) + "," + q.toGLSL(!0, 2) + "), dot(" + g.toGLSL(!0) + "," + q.toGLSL(!0, 3) + ")); // m44"), f.addDR(d,
                    fa.VECTOR_4), f.addSR(g, fa.VECTOR_4), f.addSR(q, fa.VECTOR_4, 0), f.addSR(q, fa.VECTOR_4, 1), f.addSR(q, fa.VECTOR_4, 2), f.addSR(q, fa.VECTOR_4, 3));
                break;
            case 25:
                d.mask &= 7;
                k = f.getRegisterUsage(q);
                k != fa.VECTOR_4 && k != fa.VECTOR_4_ARRAY ? (h += H.string(d.toGLSL() + " = " + g.toGLSL() + " * " + q.toGLSL(!1) + "; // m34"), f.addDR(d, fa.VECTOR_4), f.addSR(g, fa.VECTOR_4), f.addSR(q, fa.MATRIX_4_4)) : (g.sourceMask = q.sourceMask = 15, h += H.string(d.toGLSL() + " = vec3(dot(" + g.toGLSL(!0) + "," + q.toGLSL(!0, 0) + "), dot(" + g.toGLSL(!0) + "," + q.toGLSL(!0,
                    1) + "),dot(" + g.toGLSL(!0) + "," + q.toGLSL(!0, 2) + ")); // m34"), f.addDR(d, fa.VECTOR_4), f.addSR(g, fa.VECTOR_4), f.addSR(q, fa.VECTOR_4, 0), f.addSR(q, fa.VECTOR_4, 1), f.addSR(q, fa.VECTOR_4, 2));
                break;
            case 39:
                g.sourceMask = 15;
                h += H.string("if (any(lessThan(" + g.toGLSL() + ", vec4(0)))) discard;");
                f.addSR(g, fa.VECTOR_4);
                break;
            case 40:
                q = Ng.parse(p, c);
                switch (q.d) {
                    case 0:
                        2 == q.t ? (g.sourceMask = 3, f.addSaR(q, fa.SAMPLER_2D_ALPHA), h += H.string("if (" + q.toGLSL() + "_alphaEnabled) {\n"), h += H.string("\t\t" + d.toGLSL() + " = vec4(texture2D(" +
                            q.toGLSL() + ", " + g.toGLSL() + ").xyz, texture2D(" + q.toGLSL() + "_alpha, " + g.toGLSL() + ").x); // tex + alpha\n"), h += "\t} else {\n", h += H.string("\t\t" + d.toGLSL() + " = texture2D(" + q.toGLSL() + ", " + g.toGLSL() + "); // tex\n"), h += "\t}") : (g.sourceMask = 3, f.addSaR(q, fa.SAMPLER_2D), h += H.string(d.toGLSL() + " = texture2D(" + q.toGLSL() + ", " + g.toGLSL() + "); // tex"));
                        break;
                    case 1:
                        2 == q.t ? (g.sourceMask = 7, f.addSaR(q, fa.SAMPLER_CUBE_ALPHA), h += H.string("if (" + q.toGLSL() + "_alphaEnabled) {\n"), h += H.string("\t\t" + d.toGLSL() + " = vec4(textureCube(" +
                            q.toGLSL() + ", " + g.toGLSL() + ").xyz, textureCube(" + q.toGLSL() + "_alpha, " + g.toGLSL() + ").x); // tex + alpha\n"), h += "\t} else {\n", h += H.string("\t\t" + d.toGLSL() + " = textureCube(" + q.toGLSL() + ", " + g.toGLSL() + "); // tex"), h += "\t}") : (g.sourceMask = 7, h += H.string(d.toGLSL() + " = textureCube(" + q.toGLSL() + ", " + g.toGLSL() + "); // tex"), f.addSaR(q, fa.SAMPLER_CUBE))
                }
                f.addDR(d, fa.VECTOR_4);
                f.addSR(g, fa.VECTOR_4);
                null != b && (b[q.n] = q.toSamplerState());
                break;
            case 41:
                g.sourceMask = q.sourceMask = 15;
                h += H.string(d.toGLSL() + " = vec4(greaterThanEqual(" +
                    g.toGLSL() + ", " + q.toGLSL() + "))" + d.getWriteMask() + "; // ste");
                f.addDR(d, fa.VECTOR_4);
                f.addSR(g, fa.VECTOR_4);
                f.addSR(q, fa.VECTOR_4);
                break;
            case 42:
                g.sourceMask = q.sourceMask = 15;
                h += H.string(d.toGLSL() + " = vec4(lessThan(" + g.toGLSL() + ", " + q.toGLSL() + "))" + d.getWriteMask() + "; // slt");
                f.addDR(d, fa.VECTOR_4);
                f.addSR(g, fa.VECTOR_4);
                f.addSR(q, fa.VECTOR_4);
                break;
            case 44:
                g.sourceMask = q.sourceMask = 15;
                h += H.string(d.toGLSL() + " = vec4(equal(" + g.toGLSL() + ", " + q.toGLSL() + "))" + d.getWriteMask() + "; // seq");
                f.addDR(d, fa.VECTOR_4);
                f.addSR(g, fa.VECTOR_4);
                f.addSR(q, fa.VECTOR_4);
                break;
            case 45:
                g.sourceMask = q.sourceMask = 15;
                h += H.string(d.toGLSL() + " = vec4(notEqual(" + g.toGLSL() + ", " + q.toGLSL() + "))" + d.getWriteMask() + "; // sne");
                f.addDR(d, fa.VECTOR_4);
                f.addSR(g, fa.VECTOR_4);
                f.addSR(q, fa.VECTOR_4);
                break;
            default:
                throw new Wc("Opcode " + k);
        }
        h += "\n"
    }
    null == bd.limitedProfile && (d = Xe.context.getParameter(7938), bd.limitedProfile = -1 < d.indexOf("OpenGL ES") || -1 < d.indexOf("WebGL"));
    a = "" + H.string("// AGAL " + (c == Fe.VERTEX ? "vertex" : "fragment") + " shader\n");
    a = bd.limitedProfile ? a + "#version 100\n#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\n#else\nprecision mediump float;\n#endif\n" : a + "#version 120\n";
    a += H.string(f.toGLSL(!1));
    c == Fe.VERTEX && (a += "uniform vec4 vcPositionScale;\n");
    a = a + "void main() {\n" + H.string(f.toGLSL(!0));
    a += H.string(h);
    c == Fe.VERTEX && (a += "\tgl_Position *= vcPositionScale;\n");
    return a + "}\n"
};
var Mg = function() {};
g["openfl.display3D._internal._AGALConverter.DestRegister"] = Mg;
Mg.__name__ = "openfl.display3D._internal._AGALConverter.DestRegister";
Mg.parse = function(a, b) {
    var c = new Mg;
    c.programType = b;
    c.type = a >>> 24 & 15;
    c.mask = a >>> 16 & 15;
    c.n = a & 65535;
    return c
};
Mg.prototype = {
    getWriteMask: function() {
        var a = ".";
        0 != (this.mask & 1) && (a += "x");
        0 != (this.mask & 2) && (a += "y");
        0 != (this.mask & 4) && (a += "z");
        0 != (this.mask & 8) && (a += "w");
        return a
    },
    toGLSL: function(a) {
        null == a && (a = !0);
        var b = 3 == this.type ? this.programType == Fe.VERTEX ? "gl_Position" : "gl_FragColor" : bd.prefixFromType(this.type, this.programType) + this.n;
        a && 15 != this.mask && (b += this.getWriteMask());
        return b
    },
    __class__: Mg
};
var Fe = y["openfl.display3D._internal._AGALConverter.ProgramType"] = {
    __ename__: "openfl.display3D._internal._AGALConverter.ProgramType",
    __constructs__: null,
    VERTEX: {
        _hx_name: "VERTEX",
        _hx_index: 0,
        __enum__: "openfl.display3D._internal._AGALConverter.ProgramType",
        toString: r
    },
    FRAGMENT: {
        _hx_name: "FRAGMENT",
        _hx_index: 1,
        __enum__: "openfl.display3D._internal._AGALConverter.ProgramType",
        toString: r
    }
};
Fe.__constructs__ = [Fe.VERTEX, Fe.FRAGMENT];
var Cj = function() {
    this.mEntries = []
};
g["openfl.display3D._internal.RegisterMap"] =
    Cj;
Cj.__name__ = "openfl.display3D._internal.RegisterMap";
Cj.prototype = {
    add: function(a, b, c, d) {
        for (var f = 0, h = this.mEntries; f < h.length;) {
            var k = h[f];
            ++f;
            if (k.type == a && k.name == b && k.number == c) {
                if (k.usage != d) throw new Wc("Cannot use register in multiple ways yet (mat4/vec4)");
                return
            }
        }
        k = new Mk;
        k.type = a;
        k.name = b;
        k.number = c;
        k.usage = d;
        this.mEntries.push(k)
    },
    addDR: function(a, b) {
        this.add(a.type, a.toGLSL(!1), a.n, b)
    },
    addSaR: function(a, b) {
        this.add(a.type, a.toGLSL(), a.n, b)
    },
    addSR: function(a, b, c) {
        null == c && (c = 0);
        0 !=
            a.d ? (this.add(a.itype, bd.prefixFromType(a.itype, a.programType) + a.n, a.n, fa.VECTOR_4), this.add(a.type, bd.prefixFromType(a.type, a.programType) + a.o, a.o, fa.VECTOR_4_ARRAY)) : this.add(a.type, a.toGLSL(!1, c), a.n + c, b)
    },
    getRegisterUsage: function(a) {
        return 0 != a.d ? fa.VECTOR_4_ARRAY : this.getUsage(a.type, a.toGLSL(!1), a.n)
    },
    getUsage: function(a, b, c) {
        for (var d = 0, f = this.mEntries; d < f.length;) {
            var h = f[d];
            ++d;
            if (h.type == a && h.name == b && h.number == c) return h.usage
        }
        return fa.UNUSED
    },
    toGLSL: function(a) {
        this.mEntries.sort(function(a,
            b) {
            return a.number - b.number
        });
        this.mEntries.sort(function(a, b) {
            return va.__cast(a.type, vl) - va.__cast(b.type, vl)
        });
        for (var b = "", c = 0, d = this.mEntries.length; c < d;) {
            var f = c++;
            f = this.mEntries[f];
            if (!(a && 2 != f.type || !a && 2 == f.type) && 3 != f.type) {
                switch (f.type) {
                    case 0:
                        b += "attribute ";
                        break;
                    case 1:
                        b += "uniform ";
                        break;
                    case 2:
                        b += "\t";
                        break;
                    case 3:
                        break;
                    case 4:
                        b += "varying ";
                        break;
                    case 5:
                        b += "uniform ";
                        break;
                    default:
                        throw new Wc;
                }
                switch (f.usage._hx_index) {
                    case 0:
                        Ga.info("Missing switch patten: RegisterUsage.UNUSED", {
                            fileName: "openfl/display3D/_internal/AGALConverter.hx",
                            lineNumber: 751,
                            className: "openfl.display3D._internal.RegisterMap",
                            methodName: "toGLSL"
                        });
                        break;
                    case 1:
                        b += "vec4 ";
                        break;
                    case 2:
                        b += "mat4 ";
                        break;
                    case 3:
                        b += "sampler2D ";
                        break;
                    case 5:
                        b += "samplerCube ";
                        break;
                    case 7:
                        b += "vec4 "
                }
                f.usage == fa.SAMPLER_2D_ALPHA ? (b += "sampler2D ", b += H.string(f.name), b += ";\n", b += "uniform ", b += "sampler2D ", b += H.string(f.name + "_alpha"), b += ";\n", b += "uniform ", b += "bool ", b += H.string(f.name + "_alphaEnabled")) : f.usage == fa.SAMPLER_CUBE_ALPHA ?
                    (b += "samplerCube ", b += H.string(f.name), b += ";\n", b += "uniform ", b += "samplerCube ", b += H.string(f.name + "_alpha"), b += ";\n", b += "uniform ", b += "bool ", b += H.string(f.name + "_alphaEnabled")) : b = f.usage == fa.VECTOR_4_ARRAY ? b + H.string(f.name + "[128]") : b + H.string(f.name);
                b += ";\n"
            }
        }
        return b
    },
    __class__: Cj
};
var Mk = function() {};
g["openfl.display3D._internal._AGALConverter.RegisterMapEntry"] = Mk;
Mk.__name__ = "openfl.display3D._internal._AGALConverter.RegisterMapEntry";
Mk.prototype = {
    __class__: Mk
};
var fa = y["openfl.display3D._internal._AGALConverter.RegisterUsage"] = {
    __ename__: "openfl.display3D._internal._AGALConverter.RegisterUsage",
    __constructs__: null,
    UNUSED: {
        _hx_name: "UNUSED",
        _hx_index: 0,
        __enum__: "openfl.display3D._internal._AGALConverter.RegisterUsage",
        toString: r
    },
    VECTOR_4: {
        _hx_name: "VECTOR_4",
        _hx_index: 1,
        __enum__: "openfl.display3D._internal._AGALConverter.RegisterUsage",
        toString: r
    },
    MATRIX_4_4: {
        _hx_name: "MATRIX_4_4",
        _hx_index: 2,
        __enum__: "openfl.display3D._internal._AGALConverter.RegisterUsage",
        toString: r
    },
    SAMPLER_2D: {
        _hx_name: "SAMPLER_2D",
        _hx_index: 3,
        __enum__: "openfl.display3D._internal._AGALConverter.RegisterUsage",
        toString: r
    },
    SAMPLER_2D_ALPHA: {
        _hx_name: "SAMPLER_2D_ALPHA",
        _hx_index: 4,
        __enum__: "openfl.display3D._internal._AGALConverter.RegisterUsage",
        toString: r
    },
    SAMPLER_CUBE: {
        _hx_name: "SAMPLER_CUBE",
        _hx_index: 5,
        __enum__: "openfl.display3D._internal._AGALConverter.RegisterUsage",
        toString: r
    },
    SAMPLER_CUBE_ALPHA: {
        _hx_name: "SAMPLER_CUBE_ALPHA",
        _hx_index: 6,
        __enum__: "openfl.display3D._internal._AGALConverter.RegisterUsage",
        toString: r
    },
    VECTOR_4_ARRAY: {
        _hx_name: "VECTOR_4_ARRAY",
        _hx_index: 7,
        __enum__: "openfl.display3D._internal._AGALConverter.RegisterUsage",
        toString: r
    }
};
fa.__constructs__ = [fa.UNUSED, fa.VECTOR_4, fa.MATRIX_4_4, fa.SAMPLER_2D, fa.SAMPLER_2D_ALPHA, fa.SAMPLER_CUBE, fa.SAMPLER_CUBE_ALPHA, fa.VECTOR_4_ARRAY];
var Ng = function() {};
g["openfl.display3D._internal._AGALConverter.SamplerRegister"] = Ng;
Ng.__name__ = "openfl.display3D._internal._AGALConverter.SamplerRegister";
Ng.parse = function(a, b) {
    var c = new Ng;
    c.programType = b;
    b = 60;
    b = 0 == b ? new Da(a.high, a.low) : 32 > b ? new Da(a.high >> b, a.high << 32 - b | a.low >>> b) : new Da(a.high >> 31, a.high >> b - 32);
    b = b.low & 15;
    c.f = b;
    b = 56;
    b = 0 == b ? new Da(a.high, a.low) : 32 > b ? new Da(a.high >> b, a.high << 32 - b | a.low >>> b) : new Da(a.high >> 31, a.high >> b - 32);
    b = b.low & 15;
    c.m = b;
    b = 52;
    b = 0 == b ? new Da(a.high, a.low) : 32 > b ? new Da(a.high >> b, a.high << 32 - b | a.low >>> b) : new Da(a.high >> 31, a.high >> b - 32);
    b = b.low & 15;
    c.w = b;
    b = 48;
    b = 0 == b ? new Da(a.high, a.low) : 32 > b ? new Da(a.high >> b, a.high << 32 - b | a.low >>> b) : new Da(a.high >> 31, a.high >> b - 32);
    b = b.low & 15;
    c.s = b;
    b = 44;
    b = 0 == b ? new Da(a.high, a.low) : 32 > b ? new Da(a.high >> b, a.high << 32 - b | a.low >>> b) : new Da(a.high >> 31, a.high >> b - 32);
    b = b.low & 15;
    c.d = b;
    b = 40;
    b = 0 == b ? new Da(a.high, a.low) : 32 > b ? new Da(a.high >> b, a.high << 32 - b | a.low >>> b) : new Da(a.high >> 31, a.high >> b - 32);
    b = b.low & 15;
    c.t = b;
    b = 32;
    b = 0 == b ? new Da(a.high, a.low) : 32 > b ? new Da(a.high >> b, a.high << 32 - b | a.low >>> b) : new Da(a.high >> 31, a.high >> b - 32);
    b = b.low & 15;
    c.type = b;
    b = 16;
    b = 0 == b ? new Da(a.high, a.low) : 32 > b ? new Da(a.high >> b, a.high << 32 - b | a.low >>> b) : new Da(a.high >> 31, a.high >> b - 32);
    b = b.low & 255;
    c.b = b;
    b = a.low & 65535;
    c.n = b;
    return c
};
Ng.prototype = {
    toGLSL: function() {
        return bd.prefixFromType(this.type, this.programType) +
            this.n
    },
    toSamplerState: function() {
        switch (this.f) {
            case 0:
                var a = 5;
                break;
            case 1:
                a = 4;
                break;
            default:
                throw new Wc;
        }
        switch (this.m) {
            case 0:
                var b = 2;
                break;
            case 1:
                b = 1;
                break;
            case 2:
                b = 0;
                break;
            default:
                throw new Wc;
        }
        switch (this.w) {
            case 0:
                var c = 0;
                break;
            case 1:
                c = 2;
                break;
            default:
                throw new Wc;
        }
        return new bg(c, a, b, (this.b << 24 >> 24) / 8, 4 == (this.s & 4), 1 == (this.s & 1), 2 == this.t)
    },
    __class__: Ng
};
var cg = function() {};
g["openfl.display3D._internal._AGALConverter.SourceRegister"] = cg;
cg.__name__ = "openfl.display3D._internal._AGALConverter.SourceRegister";
cg.parse = function(a, b, c) {
    var d = new cg;
    d.programType = b;
    b = 63;
    b = 0 == b ? new Da(a.high, a.low) : 32 > b ? new Da(a.high >> b, a.high << 32 - b | a.low >>> b) : new Da(a.high >> 31, a.high >> b - 32);
    b = b.low & 1;
    d.d = b;
    b = 48;
    b = 0 == b ? new Da(a.high, a.low) : 32 > b ? new Da(a.high >> b, a.high << 32 - b | a.low >>> b) : new Da(a.high >> 31, a.high >> b - 32);
    b = b.low & 3;
    d.q = b;
    b = 40;
    b = 0 == b ? new Da(a.high, a.low) : 32 > b ? new Da(a.high >> b, a.high << 32 - b | a.low >>> b) : new Da(a.high >> 31, a.high >> b - 32);
    b = b.low & 15;
    d.itype = b;
    b = 32;
    b = 0 == b ? new Da(a.high, a.low) : 32 > b ? new Da(a.high >> b, a.high <<
        32 - b | a.low >>> b) : new Da(a.high >> 31, a.high >> b - 32);
    b = b.low & 15;
    d.type = b;
    b = 24;
    b = 0 == b ? new Da(a.high, a.low) : 32 > b ? new Da(a.high >> b, a.high << 32 - b | a.low >>> b) : new Da(a.high >> 31, a.high >> b - 32);
    b = b.low & 255;
    d.s = b;
    b = 16;
    b = 0 == b ? new Da(a.high, a.low) : 32 > b ? new Da(a.high >> b, a.high << 32 - b | a.low >>> b) : new Da(a.high >> 31, a.high >> b - 32);
    b = b.low & 255;
    d.o = b;
    b = a.low & 65535;
    d.n = b;
    d.sourceMask = c;
    return d
};
cg.prototype = {
    toGLSL: function(a, b) {
        null == b && (b = 0);
        null == a && (a = !0);
        if (3 == this.type) return this.programType == Fe.VERTEX ? "gl_Position" :
            "gl_FragColor";
        var c = 228 == this.s && 15 == this.sourceMask,
            d = "";
        if (5 != this.type && !c) {
            if (0 != (this.sourceMask & 1)) switch (this.s & 3) {
                case 0:
                    d += "x";
                    break;
                case 1:
                    d += "y";
                    break;
                case 2:
                    d += "z";
                    break;
                case 3:
                    d += "w"
            }
            if (0 != (this.sourceMask & 2)) switch (this.s >> 2 & 3) {
                case 0:
                    d += "x";
                    break;
                case 1:
                    d += "y";
                    break;
                case 2:
                    d += "z";
                    break;
                case 3:
                    d += "w"
            }
            if (0 != (this.sourceMask & 4)) switch (this.s >> 4 & 3) {
                case 0:
                    d += "x";
                    break;
                case 1:
                    d += "y";
                    break;
                case 2:
                    d += "z";
                    break;
                case 3:
                    d += "w"
            }
            if (0 != (this.sourceMask & 8)) switch (this.s >> 6 & 3) {
                case 0:
                    d += "x";
                    break;
                case 1:
                    d += "y";
                    break;
                case 2:
                    d += "z";
                    break;
                case 3:
                    d += "w"
            }
        }
        c = bd.prefixFromType(this.type, this.programType);
        if (0 == this.d) c += this.n + b;
        else {
            c += this.o;
            var f = "";
            switch (this.q) {
                case 0:
                    f = "x";
                    break;
                case 1:
                    f = "y";
                    break;
                case 2:
                    f = "z";
                    break;
                case 3:
                    f = "w"
            }
            f = bd.prefixFromType(this.itype, this.programType) + this.n + "." + f;
            c += "[ int(" + f + ") +" + b + "]"
        }
        a && "" != d && (c += "." + d);
        return c
    },
    __class__: cg
};
var Eh = function() {
    this.backBufferEnableDepthAndStencil = !1;
    this.blendDestinationAlphaFactor = 9;
    this.blendSourceAlphaFactor = 2;
    this.blendDestinationRGBFactor =
        9;
    this.blendSourceRGBFactor = 2;
    this.colorMaskAlpha = this.colorMaskBlue = this.colorMaskGreen = this.colorMaskRed = !0;
    this.culling = 3;
    this.depthCompareMode = 4;
    this.depthMask = !0;
    this.samplerStates = [];
    this.scissorRectangle = new na;
    this.stencilCompareMode = 0;
    this.stencilPass = this.stencilFail = this.stencilDepthFail = 5;
    this.stencilReadMask = 255;
    this.stencilReferenceValue = 0;
    this.stencilTriangleFace = 2;
    this.stencilWriteMask = 255;
    this.textures = [];
    this.__frontFaceGLCCW = !0;
    this.__glBlendEquation = 32774
};
g["openfl.display3D._internal.Context3DState"] =
    Eh;
Eh.__name__ = "openfl.display3D._internal.Context3DState";
Eh.prototype = {
    __class__: Eh
};
var Eb = function(a) {
    oa.call(this);
    this.__context = a;
    var b = this.__context.gl;
    this.__textureID = b.createTexture();
    this.__textureContext = this.__context.__context;
    if (null == Eb.__supportsBGRA) {
        Eb.__textureInternalFormat = b.RGBA;
        Eb.__supportsBGRA = !1;
        Eb.__textureFormat = b.RGBA;
        Eb.__compressedFormats = new mc;
        Eb.__compressedFormatsAlpha = new mc;
        a = b.getExtension("WEBGL_compressed_texture_s3tc");
        var c = b.getExtension("WEBGL_compressed_texture_etc1");
        b = b.getExtension("WEBKIT_WEBGL_compressed_texture_pvrtc");
        if (null != a) {
            var d = a.COMPRESSED_RGBA_S3TC_DXT1_EXT;
            Eb.__compressedFormats.h[0] = d;
            d = a.COMPRESSED_RGBA_S3TC_DXT5_EXT;
            Eb.__compressedFormatsAlpha.h[0] = d
        }
        null != c && (d = c.COMPRESSED_RGB_ETC1_WEBGL, Eb.__compressedFormats.h[2] = d, d = c.COMPRESSED_RGB_ETC1_WEBGL, Eb.__compressedFormatsAlpha.h[2] = d);
        null != b && (d = b.COMPRESSED_RGB_PVRTC_4BPPV1_IMG, Eb.__compressedFormats.h[1] = d, d = b.COMPRESSED_RGBA_PVRTC_4BPPV1_IMG, Eb.__compressedFormatsAlpha.h[1] = d)
    }
    this.__internalFormat =
        Eb.__textureInternalFormat;
    this.__format = Eb.__textureFormat
};
g["openfl.display3D.textures.TextureBase"] = Eb;
Eb.__name__ = "openfl.display3D.textures.TextureBase";
Eb.__super__ = oa;
Eb.prototype = v(oa.prototype, {
    dispose: function() {
        var a = this.__context.gl;
        null != this.__alphaTexture && (this.__alphaTexture.dispose(), this.__alphaTexture = null);
        null != this.__textureID && (a.deleteTexture(this.__textureID), this.__textureID = null);
        null != this.__glFramebuffer && (a.deleteFramebuffer(this.__glFramebuffer), this.__glFramebuffer =
            null);
        null != this.__glDepthRenderbuffer && (a.deleteRenderbuffer(this.__glDepthRenderbuffer), this.__glDepthRenderbuffer = null);
        null != this.__glStencilRenderbuffer && (a.deleteRenderbuffer(this.__glStencilRenderbuffer), this.__glStencilRenderbuffer = null)
    },
    __getGLFramebuffer: function(a, b, c) {
        b = this.__context.gl;
        null == this.__glFramebuffer && (this.__glFramebuffer = b.createFramebuffer(), this.__context.__bindGLFramebuffer(this.__glFramebuffer), b.framebufferTexture2D(b.FRAMEBUFFER, b.COLOR_ATTACHMENT0, b.TEXTURE_2D,
            this.__textureID, 0), this.__context.__enableErrorChecking && (c = b.checkFramebufferStatus(b.FRAMEBUFFER), c != b.FRAMEBUFFER_COMPLETE && Ga.warn("Error: Context3D.setRenderToTexture status:" + c + " width:" + this.__width + " height:" + this.__height, {
            fileName: "openfl/display3D/textures/TextureBase.hx",
            lineNumber: 201,
            className: "openfl.display3D.textures.TextureBase",
            methodName: "__getGLFramebuffer"
        })));
        a && null == this.__glDepthRenderbuffer && (this.__context.__bindGLFramebuffer(this.__glFramebuffer), 0 != ub.__glDepthStencil ?
            (this.__glStencilRenderbuffer = this.__glDepthRenderbuffer = b.createRenderbuffer(), b.bindRenderbuffer(b.RENDERBUFFER, this.__glDepthRenderbuffer), b.renderbufferStorage(b.RENDERBUFFER, ub.__glDepthStencil, this.__width, this.__height), b.framebufferRenderbuffer(b.FRAMEBUFFER, b.DEPTH_STENCIL_ATTACHMENT, b.RENDERBUFFER, this.__glDepthRenderbuffer)) : (this.__glDepthRenderbuffer = b.createRenderbuffer(), this.__glStencilRenderbuffer = b.createRenderbuffer(), b.bindRenderbuffer(b.RENDERBUFFER, this.__glDepthRenderbuffer),
                b.renderbufferStorage(b.RENDERBUFFER, b.DEPTH_COMPONENT16, this.__width, this.__height), b.bindRenderbuffer(b.RENDERBUFFER, this.__glStencilRenderbuffer), b.renderbufferStorage(b.RENDERBUFFER, b.STENCIL_INDEX8, this.__width, this.__height), b.framebufferRenderbuffer(b.FRAMEBUFFER, b.DEPTH_ATTACHMENT, b.RENDERBUFFER, this.__glDepthRenderbuffer), b.framebufferRenderbuffer(b.FRAMEBUFFER, b.STENCIL_ATTACHMENT, b.RENDERBUFFER, this.__glStencilRenderbuffer)), this.__context.__enableErrorChecking && (c = b.checkFramebufferStatus(b.FRAMEBUFFER),
                c != b.FRAMEBUFFER_COMPLETE && Ga.warn("Error: Context3D.setRenderToTexture status:" + c + " width:" + this.__width + " height:" + this.__height, {
                    fileName: "openfl/display3D/textures/TextureBase.hx",
                    lineNumber: 239,
                    className: "openfl.display3D.textures.TextureBase",
                    methodName: "__getGLFramebuffer"
                })), b.bindRenderbuffer(b.RENDERBUFFER, null));
        return this.__glFramebuffer
    },
    __getTexture: function() {
        return this.__textureID
    },
    __setSamplerState: function(a) {
        if (!a.equals(this.__samplerState)) {
            var b = this.__context.gl;
            this.__textureTarget ==
                this.__context.gl.TEXTURE_CUBE_MAP ? this.__context.__bindGLTextureCubeMap(this.__textureID) : this.__context.__bindGLTexture2D(this.__textureID);
            var c;
            switch (a.wrap) {
                case 0:
                    var d = c = b.CLAMP_TO_EDGE;
                    break;
                case 1:
                    c = b.CLAMP_TO_EDGE;
                    d = b.REPEAT;
                    break;
                case 2:
                    d = c = b.REPEAT;
                    break;
                case 3:
                    c = b.REPEAT;
                    d = b.CLAMP_TO_EDGE;
                    break;
                default:
                    throw new Vb("wrap bad enum");
            }
            var f = 5 == a.filter ? b.NEAREST : b.LINEAR;
            switch (a.mipfilter) {
                case 0:
                    var h = 5 == a.filter ? b.NEAREST_MIPMAP_LINEAR : b.LINEAR_MIPMAP_LINEAR;
                    break;
                case 1:
                    h = 5 == a.filter ?
                        b.NEAREST_MIPMAP_NEAREST : b.LINEAR_MIPMAP_NEAREST;
                    break;
                case 2:
                    h = 5 == a.filter ? b.NEAREST : b.LINEAR;
                    break;
                default:
                    throw new Vb("mipfiter bad enum");
            }
            b.texParameteri(this.__textureTarget, b.TEXTURE_MIN_FILTER, h);
            b.texParameteri(this.__textureTarget, b.TEXTURE_MAG_FILTER, f);
            b.texParameteri(this.__textureTarget, b.TEXTURE_WRAP_S, c);
            b.texParameteri(this.__textureTarget, b.TEXTURE_WRAP_T, d);
            null == this.__samplerState && (this.__samplerState = a.clone());
            this.__samplerState.copyFrom(a);
            return !0
        }
        return !1
    },
    __uploadFromImage: function(a) {
        var b =
            this.__context.gl,
            c;
        if (this.__textureTarget == b.TEXTURE_2D) {
            if (1 == a.buffer.bitsPerPixel) var d = c = b.ALPHA;
            else c = Eb.__textureInternalFormat, d = Eb.__textureFormat;
            this.__context.__bindGLTexture2D(this.__textureID);
            a.type == Zc.DATA || a.get_premultiplied() ? !a.get_premultiplied() && a.get_transparent() && b.pixelStorei(b.UNPACK_PREMULTIPLY_ALPHA_WEBGL, 1) : b.pixelStorei(b.UNPACK_PREMULTIPLY_ALPHA_WEBGL, 1);
            a.type == Zc.DATA ? Lc.texImage2D(b, b.TEXTURE_2D, 0, c, a.buffer.width, a.buffer.height, 0, d, b.UNSIGNED_BYTE, a.get_data()) :
                Lc.texImage2D(b, b.TEXTURE_2D, 0, c, d, b.UNSIGNED_BYTE, a.get_src());
            this.__context.__bindGLTexture2D(null)
        }
    },
    __class__: Eb
});
var Aj = function(a, b, c, d, f) {
    Eb.call(this, a);
    this.__width = this.__height = this.__size = b;
    this.__optimizeForRenderToTexture = d;
    this.__streamingLevels = f;
    this.__textureTarget = this.__context.gl.TEXTURE_CUBE_MAP;
    this.__uploadedSides = 0
};
g["openfl.display3D.textures.CubeTexture"] = Aj;
Aj.__name__ = "openfl.display3D.textures.CubeTexture";
Aj.__super__ = Eb;
Aj.prototype = v(Eb.prototype, {
    __getGLFramebuffer: function(a,
        b, c) {
        var d = this.__context.gl;
        null == this.__glFramebuffer && (this.__glFramebuffer = d.createFramebuffer(), this.__framebufferSurface = -1);
        if (this.__framebufferSurface != c && (this.__framebufferSurface = c, this.__context.__bindGLFramebuffer(this.__glFramebuffer), d.framebufferTexture2D(d.FRAMEBUFFER, d.COLOR_ATTACHMENT0, d.TEXTURE_CUBE_MAP_POSITIVE_X + c, this.__textureID, 0), this.__context.__enableErrorChecking)) {
            var f = d.checkFramebufferStatus(d.FRAMEBUFFER);
            f != d.FRAMEBUFFER_COMPLETE && Ga.error("Error: Context3D.setRenderToTexture status:" +
                f + " width:" + this.__width + " height:" + this.__height, {
                    fileName: "openfl/display3D/textures/CubeTexture.hx",
                    lineNumber: 279,
                    className: "openfl.display3D.textures.CubeTexture",
                    methodName: "__getGLFramebuffer"
                })
        }
        return Eb.prototype.__getGLFramebuffer.call(this, a, b, c)
    },
    __setSamplerState: function(a) {
        if (Eb.prototype.__setSamplerState.call(this, a)) {
            var b = this.__context.gl;
            2 == a.mipfilter || this.__samplerState.mipmapGenerated || (b.generateMipmap(b.TEXTURE_CUBE_MAP), this.__samplerState.mipmapGenerated = !0);
            if (0 != ub.__glMaxTextureMaxAnisotropy) {
                switch (a.filter) {
                    case 0:
                        a =
                            16;
                        break;
                    case 1:
                        a = 2;
                        break;
                    case 2:
                        a = 4;
                        break;
                    case 3:
                        a = 8;
                        break;
                    default:
                        a = 1
                }
                a > ub.__glMaxTextureMaxAnisotropy && (a = ub.__glMaxTextureMaxAnisotropy);
                b.texParameterf(b.TEXTURE_CUBE_MAP, ub.__glTextureMaxAnisotropy, a)
            }
            return !0
        }
        return !1
    },
    __class__: Aj
});
var Fh = function(a, b, c, d, f) {
    Eb.call(this, a);
    this.__width = b;
    this.__height = c;
    this.__optimizeForRenderToTexture = f;
    this.__textureTarget = this.__context.gl.TEXTURE_2D;
    this.uploadFromTypedArray(null);
    f && this.__getGLFramebuffer(!0, 0, 0)
};
g["openfl.display3D.textures.RectangleTexture"] =
    Fh;
Fh.__name__ = "openfl.display3D.textures.RectangleTexture";
Fh.__super__ = Eb;
Fh.prototype = v(Eb.prototype, {
    uploadFromTypedArray: function(a) {
        var b = this.__context.gl;
        this.__context.__bindGLTexture2D(this.__textureID);
        Lc.texImage2D(b, this.__textureTarget, 0, this.__internalFormat, this.__width, this.__height, 0, this.__format, b.UNSIGNED_BYTE, a);
        this.__context.__bindGLTexture2D(null)
    },
    __setSamplerState: function(a) {
        if (Eb.prototype.__setSamplerState.call(this, a)) {
            var b = this.__context.gl;
            if (0 != ub.__glMaxTextureMaxAnisotropy) {
                switch (a.filter) {
                    case 0:
                        a =
                            16;
                        break;
                    case 1:
                        a = 2;
                        break;
                    case 2:
                        a = 4;
                        break;
                    case 3:
                        a = 8;
                        break;
                    default:
                        a = 1
                }
                a > ub.__glMaxTextureMaxAnisotropy && (a = ub.__glMaxTextureMaxAnisotropy);
                b.texParameterf(b.TEXTURE_2D, ub.__glTextureMaxAnisotropy, a)
            }
            return !0
        }
        return !1
    },
    __class__: Fh
});
var zj = function(a, b, c, d, f, h) {
    Eb.call(this, a);
    this.__width = b;
    this.__height = c;
    this.__optimizeForRenderToTexture = f;
    this.__streamingLevels = h;
    a = this.__context.gl;
    this.__textureTarget = a.TEXTURE_2D;
    this.__context.__bindGLTexture2D(this.__textureID);
    Lc.texImage2D(a, this.__textureTarget,
        0, this.__internalFormat, this.__width, this.__height, 0, this.__format, a.UNSIGNED_BYTE, null);
    this.__context.__bindGLTexture2D(null);
    f && this.__getGLFramebuffer(!0, 0, 0)
};
g["openfl.display3D.textures.Texture"] = zj;
zj.__name__ = "openfl.display3D.textures.Texture";
zj.__super__ = Eb;
zj.prototype = v(Eb.prototype, {
    __setSamplerState: function(a) {
        if (Eb.prototype.__setSamplerState.call(this, a)) {
            var b = this.__context.gl;
            2 == a.mipfilter || this.__samplerState.mipmapGenerated || (b.generateMipmap(b.TEXTURE_2D), this.__samplerState.mipmapGenerated = !0);
            if (0 != ub.__glMaxTextureMaxAnisotropy) {
                switch (a.filter) {
                    case 0:
                        a = 16;
                        break;
                    case 1:
                        a = 2;
                        break;
                    case 2:
                        a = 4;
                        break;
                    case 3:
                        a = 8;
                        break;
                    default:
                        a = 1
                }
                a > ub.__glMaxTextureMaxAnisotropy && (a = ub.__glMaxTextureMaxAnisotropy);
                b.texParameterf(b.TEXTURE_2D, ub.__glTextureMaxAnisotropy, a)
            }
            return !0
        }
        return !1
    },
    __class__: zj
});
var Nk = function(a) {
    Eb.call(this, a);
    this.__textureTarget = this.__context.gl.TEXTURE_2D
};
g["openfl.display3D.textures.VideoTexture"] = Nk;
Nk.__name__ = "openfl.display3D.textures.VideoTexture";
Nk.__super__ =
    Eb;
Nk.prototype = v(Eb.prototype, {
    dispose: function() {
        null != this.__netStream && null != this.__netStream.__video && this.__netStream.__video.removeEventListener("timeupdate", l(this, this.__onTimeUpdate));
        Eb.prototype.dispose.call(this)
    },
    __onTimeUpdate: function(a) {
        null != this.__netStream && this.__netStream.__video.currentTime != this.__cacheTime && 2 <= this.__netStream.__video.readyState && this.__textureReady()
    },
    __getTexture: function() {
        if (this.__netStream.__video.currentTime != this.__cacheTime && 2 <= this.__netStream.__video.readyState) {
            var a =
                this.__context.gl;
            this.__context.__bindGLTexture2D(this.__textureID);
            Lc.texImage2D(a, a.TEXTURE_2D, 0, a.RGBA, a.RGBA, a.UNSIGNED_BYTE, this.__netStream.__video);
            this.__cacheTime = this.__netStream.__video.currentTime
        }
        return this.__textureID
    },
    __textureReady: function() {
        this.videoWidth = this.__netStream.__video.videoWidth;
        this.videoHeight = this.__netStream.__video.videoHeight;
        var a = new wa("textureReady");
        this.dispatchEvent(a)
    },
    __class__: Nk
});
var Vb = function(a, b) {
    null == b && (b = 0);
    null == a && (a = "");
    X.call(this, a);
    this.errorID = b;
    this.name = "Error";
    this.__skipStack++
};
g["openfl.errors.Error"] = Vb;
Vb.__name__ = "openfl.errors.Error";
Vb.__super__ = X;
Vb.prototype = v(X.prototype, {
    toString: function() {
        return null != this.get_message() ? this.get_message() : "Error"
    },
    __class__: Vb
});
var gg = function(a) {
    null == a && (a = "");
    Vb.call(this, a);
    this.name = "ArgumentError";
    this.__skipStack++
};
g["openfl.errors.ArgumentError"] = gg;
gg.__name__ = "openfl.errors.ArgumentError";
gg.__super__ = Vb;
gg.prototype = v(Vb.prototype, {
    __class__: gg
});
var Og = function(a) {
    null ==
        a && (a = "");
    Vb.call(this, a);
    this.name = "IOError";
    this.__skipStack++
};
g["openfl.errors.IOError"] = Og;
Og.__name__ = "openfl.errors.IOError";
Og.__super__ = Vb;
Og.prototype = v(Vb.prototype, {
    __class__: Og
});
var Hh = function(a, b) {
    Og.call(this, "End of file was encountered");
    this.name = "EOFError";
    this.errorID = 2030;
    this.__skipStack++
};
g["openfl.errors.EOFError"] = Hh;
Hh.__name__ = "openfl.errors.EOFError";
Hh.__super__ = Og;
Hh.prototype = v(Og.prototype, {
    __class__: Hh
});
var Wc = function(a) {
    null == a && (a = "");
    Vb.call(this, a, 0);
    this.name =
        "IllegalOperationError";
    this.__skipStack++
};
g["openfl.errors.IllegalOperationError"] = Wc;
Wc.__name__ = "openfl.errors.IllegalOperationError";
Wc.__super__ = Vb;
Wc.prototype = v(Vb.prototype, {
    __class__: Wc
});
var $g = function(a) {
    null == a && (a = "");
    Vb.call(this, a, 0);
    this.name = "RangeError";
    this.__skipStack++
};
g["openfl.errors.RangeError"] = $g;
$g.__name__ = "openfl.errors.RangeError";
$g.__super__ = Vb;
$g.prototype = v(Vb.prototype, {
    __class__: $g
});
var Cf = function(a) {
    null == a && (a = "");
    Vb.call(this, a, 0);
    this.name = "TypeError";
    this.__skipStack++
};
g["openfl.errors.TypeError"] = Cf;
Cf.__name__ = "openfl.errors.TypeError";
Cf.__super__ = Vb;
Cf.prototype = v(Vb.prototype, {
    __class__: Cf
});
var wa = function(a, b, c) {
    null == c && (c = !1);
    null == b && (b = !1);
    this.type = a;
    this.bubbles = b;
    this.cancelable = c;
    this.eventPhase = 2
};
g["openfl.events.Event"] = wa;
wa.__name__ = "openfl.events.Event";
wa.prototype = {
    isDefaultPrevented: function() {
        return this.__preventDefault
    },
    preventDefault: function() {
        this.cancelable && (this.__preventDefault = !0)
    },
    stopPropagation: function() {
        this.__isCanceled = !0
    },
    __class__: wa
};
var Pg = function(a, b, c, d) {
    null == d && (d = !1);
    null == c && (c = !1);
    null == b && (b = !1);
    wa.call(this, a, b, c);
    this.activating = d
};
g["openfl.events.ActivityEvent"] = Pg;
Pg.__name__ = "openfl.events.ActivityEvent";
Pg.__super__ = wa;
Pg.prototype = v(wa.prototype, {
    __class__: Pg
});
var Ce = function(a, b, c, d) {
    null == d && (d = "");
    null == c && (c = !1);
    null == b && (b = !1);
    wa.call(this, a, b, c);
    this.text = d
};
g["openfl.events.TextEvent"] = Ce;
Ce.__name__ = "openfl.events.TextEvent";
Ce.__super__ = wa;
Ce.prototype = v(wa.prototype, {
    __class__: Ce
});
var Xd = function(a, b, c, d, f) {
    null == f && (f = 0);
    null == d && (d = "");
    null == c && (c = !1);
    null == b && (b = !1);
    Ce.call(this, a, b, c, d);
    this.errorID = f
};
g["openfl.events.ErrorEvent"] = Xd;
Xd.__name__ = "openfl.events.ErrorEvent";
Xd.__super__ = Ce;
Xd.prototype = v(Ce.prototype, {
    __class__: Xd
});
var Yg = function(a) {
    this.active = !1;
    this.reset(a)
};
g["openfl.events._EventDispatcher.DispatchIterator"] = Yg;
Yg.__name__ = "openfl.events._EventDispatcher.DispatchIterator";
Yg.prototype = {
    copy: function() {
        this.isCopy || (this.list = this.list.slice(), this.isCopy = !0)
    },
    hasNext: function() {
        return this.index < this.list.length
    },
    next: function() {
        return this.list[this.index++]
    },
    remove: function(a, b) {
        if (this.active)
            if (this.isCopy) {
                b = this.index;
                for (var c = this.list.length; b < c;) {
                    var d = b++;
                    if (this.list[d] == a) {
                        this.list.splice(d, 1);
                        break
                    }
                }
            } else b < this.index && this.index--
    },
    reset: function(a) {
        this.list = a;
        this.isCopy = !1;
        this.index = 0
    },
    start: function() {
        this.active = !0
    },
    stop: function() {
        this.active = !1
    },
    __class__: Yg
};
var Bf = function(a, b, c, d) {
    d && Bf.supportsWeakReference ? this.weakRefCallback =
        new WeakRef(a) : this.callback = a;
    this.useCapture = b;
    this.priority = c;
    this.useWeakReference = d
};
g["openfl.events._EventDispatcher.Listener"] = Bf;
Bf.__name__ = "openfl.events._EventDispatcher.Listener";
Bf.prototype = {
    match: function(a, b) {
        var c = this.callback;
        return null != this.weakRefCallback && (c = this.weakRefCallback.deref(), null == c) ? !1 : c == a ? this.useCapture == b : !1
    },
    __class__: Bf
};
var $f = function(a, b, c, d, f, h) {
    null == h && (h = 0);
    null == f && (f = !1);
    null == c && (c = !1);
    null == b && (b = !1);
    wa.call(this, a, b, c);
    this.keyCode = h;
    this.shiftKey =
        f;
    this.relatedObject = d
};
g["openfl.events.FocusEvent"] = $f;
$f.__name__ = "openfl.events.FocusEvent";
$f.__super__ = wa;
$f.prototype = v(wa.prototype, {
    __class__: $f
});
var df = function(a, b, c, d, f) {
    null == f && (f = !1);
    null == d && (d = !1);
    null == c && (c = !1);
    null == b && (b = !1);
    Pg.call(this, a, b, c);
    this.fullScreen = d;
    this.interactive = f
};
g["openfl.events.FullScreenEvent"] = df;
df.__name__ = "openfl.events.FullScreenEvent";
df.__super__ = Pg;
df.prototype = v(Pg.prototype, {
    __class__: df
});
var Qg = function(a, b, c, d) {
    null == c && (c = !1);
    null == b && (b = !0);
    wa.call(this, a, b, c);
    this.device = d
};
g["openfl.events.GameInputEvent"] = Qg;
Qg.__name__ = "openfl.events.GameInputEvent";
Qg.__super__ = wa;
Qg.prototype = v(wa.prototype, {
    __class__: Qg
});
var Ih = function(a, b, c, d, f) {
    null == f && (f = !1);
    null == d && (d = 0);
    null == c && (c = !1);
    null == b && (b = !1);
    this.responseHeaders = [];
    this.status = d;
    this.redirected = f;
    wa.call(this, a, b, c)
};
g["openfl.events.HTTPStatusEvent"] = Ih;
Ih.__name__ = "openfl.events.HTTPStatusEvent";
Ih.__super__ = wa;
Ih.prototype = v(wa.prototype, {
    __class__: Ih
});
var Rg = function(a,
    b, c, d, f) {
    null == f && (f = 0);
    null == d && (d = "");
    null == c && (c = !1);
    null == b && (b = !0);
    Xd.call(this, a, b, c, d, f)
};
g["openfl.events.IOErrorEvent"] = Rg;
Rg.__name__ = "openfl.events.IOErrorEvent";
Rg.__super__ = Xd;
Rg.prototype = v(Xd.prototype, {
    __class__: Rg
});
var vj = function(a, b, c, d, f, h, k, g, p, q, u) {
    null == u && (u = !1);
    null == q && (q = !1);
    null == p && (p = !1);
    null == g && (g = !1);
    null == k && (k = !1);
    null == f && (f = 0);
    null == d && (d = 0);
    null == c && (c = !1);
    null == b && (b = !1);
    wa.call(this, a, b, c);
    this.charCode = d;
    this.keyCode = f;
    this.keyLocation = null != h ? h : 0;
    this.ctrlKey =
        k;
    this.altKey = g;
    this.shiftKey = p;
    this.controlKey = q;
    this.commandKey = u;
    this.__updateAfterEventFlag = !1
};
g["openfl.events.KeyboardEvent"] = vj;
vj.__name__ = "openfl.events.KeyboardEvent";
vj.__super__ = wa;
vj.prototype = v(wa.prototype, {
    __class__: vj
});
var Ob = function(a, b, c, d, f, h, k, g, q, u, r, m, l, x) {
    null == x && (x = 0);
    null == l && (l = !1);
    null == m && (m = !1);
    null == r && (r = 0);
    null == u && (u = !1);
    null == q && (q = !1);
    null == g && (g = !1);
    null == k && (k = !1);
    null == f && (f = 0);
    null == d && (d = 0);
    null == c && (c = !1);
    null == b && (b = !0);
    wa.call(this, a, b, c);
    this.shiftKey =
        q;
    this.altKey = g;
    this.ctrlKey = k;
    this.bubbles = b;
    this.relatedObject = h;
    this.delta = r;
    this.localX = d;
    this.localY = f;
    this.buttonDown = u;
    this.commandKey = m;
    this.controlKey = l;
    this.clickCount = x;
    this.isRelatedObjectInaccessible = !1;
    this.stageY = this.stageX = NaN;
    this.__updateAfterEventFlag = !1
};
g["openfl.events.MouseEvent"] = Ob;
Ob.__name__ = "openfl.events.MouseEvent";
Ob.__create = function(a, b, c, d, f, h, k, g) {
    null == g && (g = 0);
    a = new Ob(a, !0, !1, h.x, h.y, null, Ob.__ctrlKey, Ob.__altKey, Ob.__shiftKey, Ob.__buttonDown, g, Ob.__commandKey,
        Ob.__controlKey, c);
    a.stageX = d;
    a.stageY = f;
    a.target = k;
    return a
};
Ob.__super__ = wa;
Ob.prototype = v(wa.prototype, {
    updateAfterEvent: function() {
        this.__updateAfterEventFlag = !0
    },
    __class__: Ob
});
var Dj = function(a, b, c, d) {
    null == c && (c = !1);
    null == b && (b = !1);
    this.info = d;
    wa.call(this, a, b, c)
};
g["openfl.events.NetStatusEvent"] = Dj;
Dj.__name__ = "openfl.events.NetStatusEvent";
Dj.__super__ = wa;
Dj.prototype = v(wa.prototype, {
    __class__: Dj
});
var Wf = function(a, b, c, d, f) {
    null == f && (f = 0);
    null == d && (d = 0);
    null == c && (c = !1);
    null == b && (b = !1);
    wa.call(this, a, b, c);
    this.bytesLoaded = d;
    this.bytesTotal = f
};
g["openfl.events.ProgressEvent"] = Wf;
Wf.__name__ = "openfl.events.ProgressEvent";
Wf.__super__ = wa;
Wf.prototype = v(wa.prototype, {
    __class__: Wf
});
var Nh = function(a, b, c, d, f, h) {
    null == h && (h = !0);
    null == c && (c = !1);
    null == b && (b = !1);
    wa.call(this, a, b, c);
    this.objectMatrix = d;
    this.objectColorTransform = f;
    this.allowSmoothing = h
};
g["openfl.events.RenderEvent"] = Nh;
Nh.__name__ = "openfl.events.RenderEvent";
Nh.__super__ = wa;
Nh.prototype = v(wa.prototype, {
    __class__: Nh
});
var Ej =
    function(a, b, c) {
        null == c && (c = !1);
        null == b && (b = !1);
        wa.call(this, a, b, c);
        this.data = new Vc(0);
        this.data.__endian = 1;
        this.position = 0
    };
g["openfl.events.SampleDataEvent"] = Ej;
Ej.__name__ = "openfl.events.SampleDataEvent";
Ej.__super__ = wa;
Ej.prototype = v(wa.prototype, {
    getBufferSize: function() {
        var a = cb.toFloat(Td.get_length(this.data)) / cb.toFloat(4) / 2 | 0;
        if (0 < a) {
            if (0 != a && 0 == (a & a - 1) && 2048 <= a && 8192 >= a) return this.tempBuffer = new Float32Array(2 * a), a;
            throw new Vb("To be consistent with flash the listener function registered to SampleDataEvent has to provide 2048, 4096 or 8192 samples if targeting HTML5.");
        }
        return 0
    },
    getSamples: function(a) {
        this.data.position = 0;
        this.tempBuffer = Jl.fromBytes(Td.toBytes(this.data));
        this.leftChannel = a.outputBuffer.getChannelData(0);
        this.rightChannel = a.outputBuffer.getChannelData(1);
        for (var b = a = 0, c = cb.toFloat(Td.get_length(this.data)) / cb.toFloat(2) | 0; b < c;) {
            var d = b++;
            this.leftChannel[d] = this.tempBuffer[a++];
            this.rightChannel[d] = this.tempBuffer[a++]
        }
    },
    __class__: Ej
});
var Fj = function(a, b, c, d, f) {
    null == f && (f = 0);
    null == d && (d = "");
    null == c && (c = !1);
    null == b && (b = !1);
    Xd.call(this, a, b, c,
        d, f)
};
g["openfl.events.SecurityErrorEvent"] = Fj;
Fj.__name__ = "openfl.events.SecurityErrorEvent";
Fj.__super__ = Xd;
Fj.prototype = v(Xd.prototype, {
    __class__: Fj
});
var Sg = function(a, b, c) {
    null == c && (c = !1);
    null == b && (b = !1);
    wa.call(this, a, b, c);
    this.__updateAfterEventFlag = !1
};
g["openfl.events.TimerEvent"] = Sg;
Sg.__name__ = "openfl.events.TimerEvent";
Sg.__super__ = wa;
Sg.prototype = v(wa.prototype, {
    updateAfterEvent: function() {
        this.__updateAfterEventFlag = !0
    },
    __class__: Sg
});
var Zd = function(a, b, c, d, f, h, k, g, q, u, r, m, l, x, D, w,
    J, y, z, C) {
    null == w && (w = !1);
    null == D && (D = !1);
    null == x && (x = !1);
    null == l && (l = !1);
    null == m && (m = !1);
    null == u && (u = 0);
    null == q && (q = 0);
    null == g && (g = 0);
    null == k && (k = 0);
    null == h && (h = 0);
    null == f && (f = !1);
    null == d && (d = 0);
    null == c && (c = !1);
    null == b && (b = !0);
    wa.call(this, a, b, c);
    this.touchPointID = d;
    this.isPrimaryTouchPoint = f;
    this.localX = h;
    this.localY = k;
    this.sizeX = g;
    this.sizeY = q;
    this.pressure = u;
    this.relatedObject = r;
    this.ctrlKey = m;
    this.altKey = l;
    this.shiftKey = x;
    this.commandKey = D;
    this.controlKey = w;
    this.stageY = this.stageX = NaN;
    this.__updateAfterEventFlag = !1
};
g["openfl.events.TouchEvent"] = Zd;
Zd.__name__ = "openfl.events.TouchEvent";
Zd.__create = function(a, b, c, d, f, h) {
    a = new Zd(a, !0, !1, 0, !0, f.x, f.y, 1, 1, 1);
    a.stageX = c;
    a.stageY = d;
    a.target = h;
    return a
};
Zd.__super__ = wa;
Zd.prototype = v(wa.prototype, {
    __class__: Zd
});
var eg = function(a, b, c, d) {
    null == c && (c = !0);
    null == b && (b = !0);
    Xd.call(this, a, b, c);
    this.error = d
};
g["openfl.events.UncaughtErrorEvent"] = eg;
eg.__name__ = "openfl.events.UncaughtErrorEvent";
eg.__super__ = Xd;
eg.prototype = v(Xd.prototype, {
    __class__: eg
});
var rj = function() {
    this.__enabled = !0;
    oa.call(this)
};
g["openfl.events.UncaughtErrorEvents"] = rj;
rj.__name__ = "openfl.events.UncaughtErrorEvents";
rj.__super__ = oa;
rj.prototype = v(oa.prototype, {
    addEventListener: function(a, b, c, d, f) {
        null == f && (f = !1);
        null == d && (d = 0);
        null == c && (c = !1);
        oa.prototype.addEventListener.call(this, a, b, c, d, f);
        Object.prototype.hasOwnProperty.call(this.__eventMap.h, "uncaughtError") && (this.__enabled = !0)
    },
    removeEventListener: function(a, b, c) {
        null == c && (c = !1);
        oa.prototype.removeEventListener.call(this, a, b, c);
        Object.prototype.hasOwnProperty.call(this.__eventMap.h,
            "uncaughtError") || (this.__enabled = !1)
    },
    __class__: rj
});
var Tg = function() {
    this.__leftExtension = this.__bottomExtension = 0;
    this.__needSecondBitmapData = !0;
    this.__numShaderPasses = 0;
    this.__preserveObject = !1;
    this.__rightExtension = 0;
    this.__shaderBlendMode = 10;
    this.__topExtension = 0;
    this.__smooth = !0
};
g["openfl.filters.BitmapFilter"] = Tg;
Tg.__name__ = "openfl.filters.BitmapFilter";
Tg.prototype = {
    clone: function() {
        return new Tg
    },
    __applyFilter: function(a, b, c, d) {
        return b
    },
    __initShader: function(a, b, c) {
        return null
    },
    __class__: Tg
};
var Mc = function(a) {
    null == this.__glFragmentSource && (this.__glFragmentSource = "varying vec2 openfl_TextureCoordv;\n\n\t\tuniform sampler2D openfl_Texture;\n\t\tuniform vec2 openfl_TextureSize;\n\n\t\tvoid main(void) {\n\n\t\t\tgl_FragColor = texture2D (openfl_Texture, openfl_TextureCoordv);\n\n\t\t}");
    null == this.__glVertexSource && (this.__glVertexSource = "attribute vec4 openfl_Position;\n\t\tattribute vec2 openfl_TextureCoord;\n\n\t\tvarying vec2 openfl_TextureCoordv;\n\n\t\tuniform mat4 openfl_Matrix;\n\t\tuniform vec2 openfl_TextureSize;\n\n\t\tvoid main(void) {\n\n\t\t\topenfl_TextureCoordv = openfl_TextureCoord;\n\n\t\tgl_Position = openfl_Matrix * openfl_Position;\n\n\t\t}");
    Dd.call(this, a);
    this.__isGenerated = !0;
    this.__initGL()
};
g["openfl.filters.BitmapFilterShader"] = Mc;
Mc.__name__ = "openfl.filters.BitmapFilterShader";
Mc.__super__ = Dd;
Mc.prototype = v(Dd.prototype, {
    __class__: Mc
});
var Gj = function() {
    null == this.__glFragmentSource && (this.__glFragmentSource = "\n\t\tuniform sampler2D openfl_Texture;\n\t\tuniform vec4 uColor;\n\t\tuniform float uStrength;\n\t\tvarying vec2 vTexCoord;\n\t\tvarying vec2 vBlurCoords[6];\n\n\t\tvoid main(void)\n\t\t{\n            vec4 texel = texture2D(openfl_Texture, vTexCoord);\n\n            vec3 contributions = vec3(0.00443, 0.05399, 0.24197);\n            vec3 top = vec3(\n                texture2D(openfl_Texture, vBlurCoords[0]).a,\n                texture2D(openfl_Texture, vBlurCoords[1]).a,\n                texture2D(openfl_Texture, vBlurCoords[2]).a\n            );\n            vec3 bottom = vec3(\n                texture2D(openfl_Texture, vBlurCoords[3]).a,\n                texture2D(openfl_Texture, vBlurCoords[4]).a,\n                texture2D(openfl_Texture, vBlurCoords[5]).a\n            );\n\n            float a = texel.a * 0.39894;\n\t\t\ta += dot(top, contributions.xyz);\n            a += dot(bottom, contributions.zyx);\n\n\t\t\tgl_FragColor = uColor * clamp(a * uStrength, 0.0, 1.0);\n\t\t}\n\t");
    null == this.__glVertexSource && (this.__glVertexSource = "\n\t\tattribute vec4 openfl_Position;\n\t\tattribute vec2 openfl_TextureCoord;\n\n\t\tuniform mat4 openfl_Matrix;\n\t\tuniform vec2 openfl_TextureSize;\n\n\t\tuniform vec2 uRadius;\n\t\tvarying vec2 vTexCoord;\n\t\tvarying vec2 vBlurCoords[6];\n\n\t\tvoid main(void) {\n\n\t\t\tgl_Position = openfl_Matrix * openfl_Position;\n\t\t\tvTexCoord = openfl_TextureCoord;\n\n\t\t\tvec3 offset = vec3(0.5, 0.75, 1.0);\n\t\t\tvec2 r = uRadius / openfl_TextureSize;\n\t\t\tvBlurCoords[0] = openfl_TextureCoord - r * offset.z;\n\t\t\tvBlurCoords[1] = openfl_TextureCoord - r * offset.y;\n\t\t\tvBlurCoords[2] = openfl_TextureCoord - r * offset.x;\n\t\t\tvBlurCoords[3] = openfl_TextureCoord + r * offset.x;\n\t\t\tvBlurCoords[4] = openfl_TextureCoord + r * offset.y;\n\t\t\tvBlurCoords[5] = openfl_TextureCoord + r * offset.z;\n\t\t}\n\t");
    Mc.call(this);
    this.uRadius.value = [0, 0];
    this.uColor.value = [0, 0, 0, 0];
    this.uStrength.value = [1];
    this.__isGenerated = !0;
    this.__initGL()
};
g["openfl.filters._GlowFilter.BlurAlphaShader"] = Gj;
Gj.__name__ = "openfl.filters._GlowFilter.BlurAlphaShader";
Gj.__super__ = Mc;
Gj.prototype = v(Mc.prototype, {
    __class__: Gj
});
var Hj = function() {
    null == this.__glFragmentSource && (this.__glFragmentSource = "\n\t\tuniform sampler2D openfl_Texture;\n\t\tuniform sampler2D sourceBitmap;\n\t\tvarying vec4 textureCoords;\n\n\t\tvoid main(void) {\n\t\t\tvec4 src = texture2D(sourceBitmap, textureCoords.xy);\n\t\t\tvec4 glow = texture2D(openfl_Texture, textureCoords.zw);\n\n\t\t\tgl_FragColor = glow * (1.0 - src.a);\n\t\t}\n\t");
    null == this.__glVertexSource && (this.__glVertexSource = "attribute vec4 openfl_Position;\n\t\tattribute vec2 openfl_TextureCoord;\n\t\tuniform mat4 openfl_Matrix;\n\t\tuniform vec2 openfl_TextureSize;\n\t\tuniform vec2 offset;\n\t\tvarying vec4 textureCoords;\n\n\t\tvoid main(void) {\n\t\t\tgl_Position = openfl_Matrix * openfl_Position;\n\t\t\ttextureCoords = vec4(openfl_TextureCoord, openfl_TextureCoord - offset / openfl_TextureSize);\n\t\t}\n\t");
    Mc.call(this);
    this.offset.value = [0, 0];
    this.__isGenerated = !0;
    this.__initGL()
};
g["openfl.filters._GlowFilter.CombineKnockoutShader"] = Hj;
Hj.__name__ = "openfl.filters._GlowFilter.CombineKnockoutShader";
Hj.__super__ = Mc;
Hj.prototype = v(Mc.prototype, {
    __class__: Hj
});
var Ij = function() {
    null == this.__glFragmentSource && (this.__glFragmentSource = "\n\t\tuniform sampler2D openfl_Texture;\n\t\tuniform sampler2D sourceBitmap;\n\t\tvarying vec4 textureCoords;\n\n\t\tvoid main(void) {\n\t\t\tvec4 src = texture2D(sourceBitmap, textureCoords.xy);\n\t\t\tvec4 glow = texture2D(openfl_Texture, textureCoords.zw);\n\n\t\t\tgl_FragColor = src + glow * (1.0 - src.a);\n\t\t}\n\t");
    null == this.__glVertexSource && (this.__glVertexSource = "attribute vec4 openfl_Position;\n\t\tattribute vec2 openfl_TextureCoord;\n\t\tuniform mat4 openfl_Matrix;\n\t\tuniform vec2 openfl_TextureSize;\n\t\tuniform vec2 offset;\n\t\tvarying vec4 textureCoords;\n\n\t\tvoid main(void) {\n\t\t\tgl_Position = openfl_Matrix * openfl_Position;\n\t\t\ttextureCoords = vec4(openfl_TextureCoord, openfl_TextureCoord - offset / openfl_TextureSize);\n\t\t}\n\t");
    Mc.call(this);
    this.offset.value = [0, 0];
    this.__isGenerated = !0;
    this.__initGL()
};
g["openfl.filters._GlowFilter.CombineShader"] = Ij;
Ij.__name__ = "openfl.filters._GlowFilter.CombineShader";
Ij.__super__ = Mc;
Ij.prototype = v(Mc.prototype, {
    __class__: Ij
});
var Jj = function() {
    null == this.__glFragmentSource && (this.__glFragmentSource = "\n\t\tuniform sampler2D openfl_Texture;\n\t\tuniform sampler2D sourceBitmap;\n\t\tvarying vec4 textureCoords;\n\n\t\tvoid main(void) {\n\t\t\tvec4 src = texture2D(sourceBitmap, textureCoords.xy);\n\t\t\tvec4 glow = texture2D(openfl_Texture, textureCoords.zw);\n\n\t\t\tgl_FragColor = glow * src.a;\n\t\t}\n\t");
    null == this.__glVertexSource && (this.__glVertexSource = "attribute vec4 openfl_Position;\n\t\tattribute vec2 openfl_TextureCoord;\n\t\tuniform mat4 openfl_Matrix;\n\t\tuniform vec2 openfl_TextureSize;\n\t\tuniform vec2 offset;\n\t\tvarying vec4 textureCoords;\n\n\t\tvoid main(void) {\n\t\t\tgl_Position = openfl_Matrix * openfl_Position;\n\t\t\ttextureCoords = vec4(openfl_TextureCoord, openfl_TextureCoord - offset / openfl_TextureSize);\n\t\t}\n\t");
    Mc.call(this);
    this.offset.value = [0, 0];
    this.__isGenerated = !0;
    this.__initGL()
};
g["openfl.filters._GlowFilter.InnerCombineKnockoutShader"] = Jj;
Jj.__name__ = "openfl.filters._GlowFilter.InnerCombineKnockoutShader";
Jj.__super__ = Mc;
Jj.prototype = v(Mc.prototype, {
    __class__: Jj
});
var Kj = function() {
    null == this.__glFragmentSource && (this.__glFragmentSource = "\n\t\tuniform sampler2D openfl_Texture;\n\t\tuniform sampler2D sourceBitmap;\n\t\tvarying vec4 textureCoords;\n\n\t\tvoid main(void) {\n\t\t\tvec4 src = texture2D(sourceBitmap, textureCoords.xy);\n\t\t\tvec4 glow = texture2D(openfl_Texture, textureCoords.zw);\n\n\t\t\tgl_FragColor = vec4((src.rgb * (1.0 - glow.a)) + (glow.rgb * src.a), src.a);\n\t\t}\n\t");
    null == this.__glVertexSource && (this.__glVertexSource = "attribute vec4 openfl_Position;\n\t\tattribute vec2 openfl_TextureCoord;\n\t\tuniform mat4 openfl_Matrix;\n\t\tuniform vec2 openfl_TextureSize;\n\t\tuniform vec2 offset;\n\t\tvarying vec4 textureCoords;\n\n\t\tvoid main(void) {\n\t\t\tgl_Position = openfl_Matrix * openfl_Position;\n\t\t\ttextureCoords = vec4(openfl_TextureCoord, openfl_TextureCoord - offset / openfl_TextureSize);\n\t\t}\n\t");
    Mc.call(this);
    this.offset.value = [0, 0];
    this.__isGenerated = !0;
    this.__initGL()
};
g["openfl.filters._GlowFilter.InnerCombineShader"] = Kj;
Kj.__name__ = "openfl.filters._GlowFilter.InnerCombineShader";
Kj.__super__ = Mc;
Kj.prototype = v(Mc.prototype, {
    __class__: Kj
});
var Lj = function() {
    null == this.__glFragmentSource && (this.__glFragmentSource = "\n\t\tuniform sampler2D openfl_Texture;\n\t\tvarying vec2 vTexCoord;\n\n\t\tvoid main(void) {\n\t\t\tvec4 texel = texture2D(openfl_Texture, vTexCoord);\n\t\t\tgl_FragColor = vec4(texel.rgb, 1.0 - texel.a);\n\t\t}\n\t");
    null == this.__glVertexSource &&
        (this.__glVertexSource = "\n\t\tattribute vec4 openfl_Position;\n\t\tattribute vec2 openfl_TextureCoord;\n\t\tuniform mat4 openfl_Matrix;\n\t\tvarying vec2 vTexCoord;\n\n\t\tvoid main(void) {\n\t\t\tgl_Position = openfl_Matrix * openfl_Position;\n\t\t\tvTexCoord = openfl_TextureCoord;\n\t\t}\n\t");
    Mc.call(this);
    this.__isGenerated = !0;
    this.__initGL()
};
g["openfl.filters._GlowFilter.InvertAlphaShader"] = Lj;
Lj.__name__ = "openfl.filters._GlowFilter.InvertAlphaShader";
Lj.__super__ = Mc;
Lj.prototype = v(Mc.prototype, {
    __class__: Lj
});
var Sc = function(a, b, c, d, f, h, k, g) {
    null == g && (g = !1);
    null == k && (k = !1);
    null == h && (h = 1);
    null == f && (f = 2);
    null == d && (d = 6);
    null == c && (c = 6);
    null == b && (b = 1);
    null == a && (a = 16711680);
    Tg.call(this);
    this.__color = a;
    this.__alpha = b;
    this.__blurX = c;
    this.__blurY = d;
    this.__strength = f;
    this.__inner = k;
    this.__knockout = g;
    this.__quality = h;
    this.__updateSize();
    this.__renderDirty = this.__preserveObject = this.__needSecondBitmapData = !0
};
g["openfl.filters.GlowFilter"] = Sc;
Sc.__name__ = "openfl.filters.GlowFilter";
Sc.__super__ = Tg;
Sc.prototype = v(Tg.prototype, {
    clone: function() {
        return new Sc(this.__color, this.__alpha, this.__blurX, this.__blurY, this.__strength, this.__quality, this.__inner, this.__knockout)
    },
    __applyFilter: function(a, b, c, d) {
        var f = this.__color >> 16 & 255,
            h = this.__color >> 8 & 255,
            k = this.__color & 255;
        c = bc.gaussianBlur(a.image, b.image, c.__toLimeRectangle(), d.__toLimeVector2(), this.__blurX, this.__blurY, this.__quality, this.__strength);
        c.colorTransform(c.get_rect(), (new Tb(0, 0, 0, this.__alpha, f, h, k, 0)).__toLimeColorMatrix());
        return c ==
            a.image ? a : b
    },
    __initShader: function(a, b, c) {
        if (this.__inner && 0 == b) return Sc.__invertAlphaShader;
        a = b - (this.__inner ? 1 : 0);
        b = this.__horizontalPasses + this.__verticalPasses;
        if (a < b) {
            var d = Sc.__blurAlphaShader;
            a < this.__horizontalPasses ? (c = .5 * Math.pow(.5, a >> 1), d.uRadius.value[0] = this.get_blurX() * c, d.uRadius.value[1] = 0) : (c = .5 * Math.pow(.5, a - this.__horizontalPasses >> 1), d.uRadius.value[0] = 0, d.uRadius.value[1] = this.get_blurY() * c);
            d.uColor.value[0] = (this.get_color() >> 16 & 255) / 255;
            d.uColor.value[1] = (this.get_color() >>
                8 & 255) / 255;
            d.uColor.value[2] = (this.get_color() & 255) / 255;
            d.uColor.value[3] = this.get_alpha();
            d.uStrength.value[0] = a == b - 1 ? this.__strength : 1;
            return d
        }
        if (this.__inner) {
            if (this.__knockout) return d = Sc.__innerCombineKnockoutShader, d.sourceBitmap.input = c, d.offset.value[0] = 0, d.offset.value[1] = 0, d;
            d = Sc.__innerCombineShader
        } else {
            if (this.__knockout) return d = Sc.__combineKnockoutShader, d.sourceBitmap.input = c, d.offset.value[0] = 0, d.offset.value[1] = 0, d;
            d = Sc.__combineShader
        }
        d.sourceBitmap.input = c;
        d.offset.value[0] =
            0;
        d.offset.value[1] = 0;
        return d
    },
    __updateSize: function() {
        this.__rightExtension = this.__leftExtension = 0 < this.__blurX ? Math.ceil(1.5 * this.__blurX) : 0;
        this.__bottomExtension = this.__topExtension = 0 < this.__blurY ? Math.ceil(1.5 * this.__blurY) : 0;
        this.__calculateNumShaderPasses()
    },
    __calculateNumShaderPasses: function() {
        this.__horizontalPasses = 0 >= this.__blurX ? 0 : Math.round(this.__quality / 4 * this.__blurX) + 1;
        this.__verticalPasses = 0 >= this.__blurY ? 0 : Math.round(this.__quality / 4 * this.__blurY) + 1;
        this.__numShaderPasses = this.__horizontalPasses +
            this.__verticalPasses + (this.__inner ? 2 : 1)
    },
    get_alpha: function() {
        return this.__alpha
    },
    get_blurX: function() {
        return this.__blurX
    },
    get_blurY: function() {
        return this.__blurY
    },
    get_color: function() {
        return this.__color
    },
    __class__: Sc,
    __properties__: {
        get_color: "get_color",
        get_blurY: "get_blurY",
        get_blurX: "get_blurX",
        get_alpha: "get_alpha"
    }
});
var wj = function(a) {
    null != a && 16 == a.get_length() ? this.rawData = a.concat(null) : this.rawData = la.toFloatVector(null, null, null, [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1])
};
g["openfl.geom.Matrix3D"] =
    wj;
wj.__name__ = "openfl.geom.Matrix3D";
wj.prototype = {
    append: function(a) {
        var b = this.rawData.get(0),
            c = this.rawData.get(4),
            d = this.rawData.get(8),
            f = this.rawData.get(12),
            h = this.rawData.get(1),
            k = this.rawData.get(5),
            g = this.rawData.get(9),
            q = this.rawData.get(13),
            u = this.rawData.get(2),
            r = this.rawData.get(6),
            m = this.rawData.get(10),
            l = this.rawData.get(14),
            x = this.rawData.get(3),
            D = this.rawData.get(7),
            w = this.rawData.get(11),
            J = this.rawData.get(15),
            y = a.rawData.get(0),
            z = a.rawData.get(4),
            C = a.rawData.get(8),
            t = a.rawData.get(12),
            v = a.rawData.get(1),
            G = a.rawData.get(5),
            F = a.rawData.get(9),
            I = a.rawData.get(13),
            K = a.rawData.get(2),
            eb = a.rawData.get(6),
            H = a.rawData.get(10),
            O = a.rawData.get(14),
            lb = a.rawData.get(3),
            Va = a.rawData.get(7),
            Y = a.rawData.get(11);
        a = a.rawData.get(15);
        this.rawData.set(0, b * y + h * z + u * C + x * t);
        this.rawData.set(1, b * v + h * G + u * F + x * I);
        this.rawData.set(2, b * K + h * eb + u * H + x * O);
        this.rawData.set(3, b * lb + h * Va + u * Y + x * a);
        this.rawData.set(4, c * y + k * z + r * C + D * t);
        this.rawData.set(5, c * v + k * G + r * F + D * I);
        this.rawData.set(6, c * K + k * eb + r * H + D * O);
        this.rawData.set(7,
            c * lb + k * Va + r * Y + D * a);
        this.rawData.set(8, d * y + g * z + m * C + w * t);
        this.rawData.set(9, d * v + g * G + m * F + w * I);
        this.rawData.set(10, d * K + g * eb + m * H + w * O);
        this.rawData.set(11, d * lb + g * Va + m * Y + w * a);
        this.rawData.set(12, f * y + q * z + l * C + J * t);
        this.rawData.set(13, f * v + q * G + l * F + J * I);
        this.rawData.set(14, f * K + q * eb + l * H + J * O);
        this.rawData.set(15, f * lb + q * Va + l * Y + J * a)
    },
    appendTranslation: function(a, b, c) {
        var d = this.rawData;
        d.set(12, d.get(12) + a);
        d = this.rawData;
        d.set(13, d.get(13) + b);
        d = this.rawData;
        d.set(14, d.get(14) + c)
    },
    copyRawDataFrom: function(a, b, c) {
        null ==
            c && (c = !1);
        null == b && (b = 0);
        c && this.transpose();
        for (var d = 0, f = a.get_length() - b; d < f;) {
            var h = d++;
            this.rawData.set(h, a.get(h + b))
        }
        c && this.transpose()
    },
    identity: function() {
        this.rawData = la.toFloatVector(null, null, null, [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1])
    },
    transpose: function() {
        var a = this.rawData.copy();
        this.rawData.set(1, a.get(4));
        this.rawData.set(2, a.get(8));
        this.rawData.set(3, a.get(12));
        this.rawData.set(4, a.get(1));
        this.rawData.set(6, a.get(9));
        this.rawData.set(7, a.get(13));
        this.rawData.set(8, a.get(2));
        this.rawData.set(9,
            a.get(6));
        this.rawData.set(11, a.get(14));
        this.rawData.set(12, a.get(3));
        this.rawData.set(13, a.get(7));
        this.rawData.set(14, a.get(11))
    },
    __class__: wj
};
var Oh = function(a) {
    this.__colorTransform = new Tb;
    this.concatenatedColorTransform = new Tb;
    this.pixelBounds = new na;
    this.__displayObject = a;
    this.__hasMatrix = !0
};
g["openfl.geom.Transform"] = Oh;
Oh.__name__ = "openfl.geom.Transform";
Oh.prototype = {
    get_colorTransform: function() {
        return this.__colorTransform.__clone()
    },
    get_matrix: function() {
        return this.__hasMatrix ? this.__displayObject.__transform.clone() :
            null
    },
    set_matrix: function(a) {
        if (null == a) return this.__hasMatrix = !1, null;
        this.__hasMatrix = !0;
        this.__hasMatrix3D = !1;
        null != this.__displayObject && this.__setTransform(a.a, a.b, a.c, a.d, a.tx, a.ty);
        return a
    },
    __setTransform: function(a, b, c, d, f, h) {
        if (null != this.__displayObject) {
            var k = this.__displayObject.__transform;
            if (k.a != a || k.b != b || k.c != c || k.d != d || k.tx != f || k.ty != h) {
                var g = 0 == b ? a : Math.sqrt(a * a + b * b);
                var q = 0 == c ? d : Math.sqrt(c * c + d * d);
                this.__displayObject.__scaleX = g;
                this.__displayObject.__scaleY = q;
                g = 180 / Math.PI *
                    Math.atan2(d, c) - 90;
                g != this.__displayObject.__rotation && (this.__displayObject.__rotation = g, g *= Math.PI / 180, this.__displayObject.__rotationSine = Math.sin(g), this.__displayObject.__rotationCosine = Math.cos(g));
                k.a = a;
                k.b = b;
                k.c = c;
                k.d = d;
                k.tx = f;
                k.ty = h;
                this.__displayObject.__setTransformDirty()
            }
        }
    },
    __class__: Oh,
    __properties__: {
        set_matrix: "set_matrix",
        get_matrix: "get_matrix",
        get_colorTransform: "get_colorTransform"
    }
};
var ch = function(a, b, c, d) {
    null == d && (d = 0);
    null == c && (c = 0);
    null == b && (b = 0);
    null == a && (a = 0);
    this.w = d;
    this.x = a;
    this.y = b;
    this.z = c
};
g["openfl.geom.Vector3D"] = ch;
ch.__name__ = "openfl.geom.Vector3D";
ch.prototype = {
    __class__: ch
};
var Ok = function(a, b) {
    this.__webAudioContext = null;
    this.__urlLoading = !1;
    oa.call(this, this);
    this.bytesTotal = this.bytesLoaded = 0;
    this.isBuffering = !1;
    this.url = null;
    null != a && this.load(a, b);
    null == a && null != Ge.context && "web" == Ge.context.type && (this.__webAudioContext = Ge.context.web)
};
g["openfl.media.Sound"] = Ok;
Ok.__name__ = "openfl.media.Sound";
Ok.__super__ = oa;
Ok.prototype = v(oa.prototype, {
    load: function(a,
        b) {
        var c = this;
        this.url = a.url;
        this.__urlLoading = !0;
        this.dispatchEvent(new wa("open"));
        a = Fa.getLibrary("default");
        if (null != a && Object.prototype.hasOwnProperty.call(a.cachedAudioBuffers.h, this.url)) a = a.cachedAudioBuffers.h[this.url], b = null != a && null != a.data ? a.data.byteLength : 0, this.AudioBuffer_onURLProgress(b, b), this.AudioBuffer_onURLLoad(a);
        else Bc.loadFromFile(this.url).onProgress(l(this, this.AudioBuffer_onURLProgress)).onComplete(l(this, this.AudioBuffer_onURLLoad)).onError(function(a) {
            c.AudioBuffer_onURLLoad(null)
        })
    },
    AudioBuffer_onURLLoad: function(a) {
        this.__urlLoading = !1;
        null == a ? this.dispatchEvent(new Rg("ioError")) : (this.__buffer = a, this.dispatchEvent(new wa("complete")), null != this.__pendingSoundChannel && (this.__pendingAudioSource.buffer = this.__buffer, this.__pendingAudioSource.init(), this.__pendingSoundChannel.__initAudioSource(this.__pendingAudioSource)));
        this.__pendingAudioSource = this.__pendingSoundChannel = null
    },
    AudioBuffer_onURLProgress: function(a, b) {
        var c = new Wf("progress");
        c.bytesLoaded = a;
        c.bytesTotal = b;
        this.dispatchEvent(c)
    },
    __class__: Ok
});
var Pk = function(a, b, c) {
    this.__firstRun = !0;
    oa.call(this, this);
    this.__sound = a;
    this.rightPeak = this.leftPeak = 1;
    this.__soundTransform = null != c ? c : new dg;
    this.__initAudioSource(b);
    ve.__registerSoundChannel(this)
};
g["openfl.media.SoundChannel"] = Pk;
Pk.__name__ = "openfl.media.SoundChannel";
Pk.__super__ = oa;
Pk.prototype = v(oa.prototype, {
    stop: function() {
        ve.__unregisterSoundChannel(this);
        this.__isValid && (null != this.__processor && (this.__processor.disconnect(), this.__processor =
            this.__processor.onaudioprocess = null), this.__audioSource.stop(), this.__dispose())
    },
    __dispose: function() {
        this.__isValid && (this.__audioSource.onComplete.remove(l(this, this.audioSource_onComplete)), this.__audioSource.dispose(), this.__audioSource = null, this.__isValid = !1)
    },
    __startSampleData: function() {
        var a = this,
            b = this.__sound.__webAudioContext;
        if (null != b) {
            this.__sampleDataEvent = new Ej("sampleData");
            this.__sound.dispatchEvent(this.__sampleDataEvent);
            var c = this.__sampleDataEvent.getBufferSize();
            0 == c ? Ra.setTimeout(function() {
                a.stop();
                a.dispatchEvent(new wa("soundComplete"))
            }, 1) : (this.__processor = b.createScriptProcessor(c, 0, 2), this.__processor.connect(b.destination), this.__processor.onaudioprocess = l(this, this.onSample), b.resume())
        }
    },
    __updateTransform: function() {
        this.set_soundTransform(this.get_soundTransform())
    },
    __initAudioSource: function(a) {
        this.__audioSource = a;
        null != this.__audioSource && (this.__audioSource.onComplete.add(l(this, this.audioSource_onComplete)), this.__isValid = !0, this.__audioSource.play())
    },
    get_position: function() {
        return this.__isValid ?
            this.__audioSource.get_currentTime() + this.__audioSource.offset : 0
    },
    set_position: function(a) {
        if (!this.__isValid) return 0;
        this.__audioSource.set_currentTime((a | 0) - this.__audioSource.offset);
        return a
    },
    get_soundTransform: function() {
        return this.__soundTransform.clone()
    },
    set_soundTransform: function(a) {
        if (null != a) {
            this.__soundTransform.pan = a.pan;
            this.__soundTransform.volume = a.volume;
            var b = ve.__soundTransform.pan + this.__soundTransform.pan; - 1 > b && (b = -1);
            1 < b && (b = 1);
            var c = ve.__soundTransform.volume * this.__soundTransform.volume;
            this.__isValid && (this.__audioSource.set_gain(c), c = this.__audioSource.get_position(), c.x = b, c.z = -1 * Math.sqrt(1 - Math.pow(b, 2)), this.__audioSource.set_position(c))
        }
        return a
    },
    audioSource_onComplete: function() {
        ve.__unregisterSoundChannel(this);
        this.__dispose();
        this.dispatchEvent(new wa("soundComplete"))
    },
    onSample: function(a) {
        if (this.__firstRun) {
            var b = !0;
            this.__firstRun = !1
        } else Td.set_length(this.__sampleDataEvent.data, 0), this.__sound.dispatchEvent(this.__sampleDataEvent), b = cb.gt(Td.get_length(this.__sampleDataEvent.data),
            0);
        b ? this.__sampleDataEvent.getSamples(a) : (this.stop(), this.dispatchEvent(new wa("soundComplete")))
    },
    __class__: Pk,
    __properties__: {
        set_soundTransform: "set_soundTransform",
        get_soundTransform: "get_soundTransform",
        set_position: "set_position",
        get_position: "get_position"
    }
});
var Cl = function() {};
g["openfl.media.SoundLoaderContext"] = Cl;
Cl.__name__ = "openfl.media.SoundLoaderContext";
var dg = function(a, b) {
    null == b && (b = 0);
    null == a && (a = 1);
    this.volume = a;
    this.pan = b;
    this.rightToRight = this.rightToLeft = this.leftToRight =
        this.leftToLeft = 0
};
g["openfl.media.SoundTransform"] = dg;
dg.__name__ = "openfl.media.SoundTransform";
dg.prototype = {
    clone: function() {
        return new dg(this.volume, this.pan)
    },
    __class__: dg
};
var ve = function() {};
g["openfl.media.SoundMixer"] = ve;
ve.__name__ = "openfl.media.SoundMixer";
ve.__registerSoundChannel = function(a) {
    ve.__soundChannels.push(a)
};
ve.__unregisterSoundChannel = function(a) {
    N.remove(ve.__soundChannels, a)
};
var Qk = function(a, b) {
    null == b && (b = 240);
    null == a && (a = 320);
    S.call(this);
    this.__drawableType = 8;
    this.__width =
        a;
    this.__height = b;
    this.__textureTime = -1;
    this.smoothing = !1;
    this.deblocking = 0
};
g["openfl.media.Video"] = Qk;
Qk.__name__ = "openfl.media.Video";
Qk.__super__ = S;
Qk.prototype = v(S.prototype, {
    __enterFrame: function(a) {
        this.__renderable && null != this.__stream && !this.__renderDirty && (this.__renderDirty = !0, this.__setParentRenderDirty())
    },
    __getBounds: function(a, b) {
        var c = na.__pool.get();
        c.setTo(0, 0, this.__width, this.__height);
        c.__transform(c, b);
        a.__expand(c.x, c.y, c.width, c.height);
        na.__pool.release(c)
    },
    __getIndexBuffer: function(a) {
        if (null ==
            this.__indexBuffer || this.__indexBufferContext != a.__context) this.__indexBufferData = new Uint16Array(6), this.__indexBufferData[0] = 0, this.__indexBufferData[1] = 1, this.__indexBufferData[2] = 2, this.__indexBufferData[3] = 2, this.__indexBufferData[4] = 1, this.__indexBufferData[5] = 3, this.__indexBufferContext = a.__context, this.__indexBuffer = a.createIndexBuffer(6), this.__indexBuffer.uploadFromTypedArray(this.__indexBufferData);
        return this.__indexBuffer
    },
    __getTexture: function(a) {
        if (null == this.__stream || null == this.__stream.__video) return null;
        var b = a.__context.webgl,
            c = b.RGBA,
            d = b.RGBA;
        this.__stream.__closed || this.__stream.__video.currentTime == this.__textureTime || (null == this.__texture && (this.__texture = a.createRectangleTexture(this.__stream.__video.videoWidth, this.__stream.__video.videoHeight, 1, !1)), a.__bindGLTexture2D(this.__texture.__textureID), Lc.texImage2D(b, b.TEXTURE_2D, 0, c, d, b.UNSIGNED_BYTE, this.__stream.__video), this.__textureTime = this.__stream.__video.currentTime);
        return this.__texture
    },
    __getVertexBuffer: function(a) {
        if (null == this.__vertexBuffer ||
            this.__vertexBufferContext != a.__context || this.__currentWidth != this.get_width() || this.__currentHeight != this.get_height()) this.__currentWidth = this.get_width(), this.__currentHeight = this.get_height(), this.__vertexBufferData = new Float32Array(20), this.__vertexBufferData[0] = this.get_width(), this.__vertexBufferData[1] = this.get_height(), this.__vertexBufferData[3] = 1, this.__vertexBufferData[4] = 1, this.__vertexBufferData[6] = this.get_height(), this.__vertexBufferData[9] = 1, this.__vertexBufferData[10] = this.get_width(),
            this.__vertexBufferData[13] = 1, this.__vertexBufferContext = a.__context, this.__vertexBuffer = a.createVertexBuffer(3, 5), this.__vertexBuffer.uploadFromTypedArray(ih.toArrayBufferView(this.__vertexBufferData));
        return this.__vertexBuffer
    },
    __hitTest: function(a, b, c, d, f, h) {
        if (!h.get_visible() || this.__isMask || null != this.get_mask() && !this.get_mask().__hitTestMask(a, b)) return !1;
        this.__getRenderTransform();
        var k = this.__renderTransform,
            g = k.a * k.d - k.b * k.c;
        c = 0 == g ? -k.tx : 1 / g * (k.c * (k.ty - b) + k.d * (a - k.tx));
        k = this.__renderTransform;
        g = k.a * k.d - k.b * k.c;
        a = 0 == g ? -k.ty : 1 / g * (k.a * (b - k.ty) + k.b * (k.tx - a));
        return 0 < c && 0 < a && c <= this.__width && a <= this.__height ? (null == d || f || d.push(h), !0) : !1
    },
    __hitTestMask: function(a, b) {
        var c = I.__pool.get();
        c.setTo(a, b);
        this.__globalToLocal(c, c);
        a = 0 < c.x && 0 < c.y && c.x <= this.__width && c.y <= this.__height;
        I.__pool.release(c);
        return a
    },
    get_height: function() {
        return this.__height * this.get_scaleY()
    },
    set_height: function(a) {
        if (1 != this.get_scaleY() || a != this.__height) this.__setTransformDirty(), this.__dirty = !0;
        this.set_scaleY(1);
        return this.__height = a
    },
    get_width: function() {
        return this.__width * this.__scaleX
    },
    set_width: function(a) {
        if (1 != this.__scaleX || this.__width != a) this.__setTransformDirty(), this.__dirty = !0;
        this.set_scaleX(1);
        return this.__width = a
    },
    __class__: Qk
});
var Th = function(a, b, c) {
    this.description = a;
    this.extension = b;
    this.macType = c
};
g["openfl.net.FileFilter"] = Th;
Th.__name__ = "openfl.net.FileFilter";
Th.prototype = {
    __class__: Th
};
var Gf = function() {
    oa.call(this);
    this.__inputControl = window.document.createElement("input");
    this.__inputControl.setAttribute("type",
        "file");
    this.__inputControl.onclick = function(a) {
        a.cancelBubble = !0;
        a.stopPropagation()
    }
};
g["openfl.net.FileReference"] = Gf;
Gf.__name__ = "openfl.net.FileReference";
Gf.__super__ = oa;
Gf.prototype = v(oa.prototype, {
    browse: function(a) {
        var b = this,
            c = this.__path = this.__data = null;
        if (null != a) {
            c = [];
            for (var d = 0; d < a.length;) {
                var f = a[d];
                ++d;
                c.push(O.replace(O.replace(f.extension, "*.", "."), ";", ","))
            }
            c = c.join(",")
        }
        null != c ? this.__inputControl.setAttribute("accept", c) : this.__inputControl.removeAttribute("accept");
        this.__inputControl.onchange =
            function() {
                if (0 == b.__inputControl.files.length) b.dispatchEvent(new wa("cancel"));
                else {
                    var a = b.__inputControl.files[0],
                        c = new Date(a.lastModified);
                    b.modificationDate = c;
                    b.creationDate = b.get_modificationDate();
                    b.size = a.size;
                    c = Ud.extension(a.name);
                    b.type = "." + c;
                    b.name = Ud.withoutDirectory(a.name);
                    b.__path = a.name;
                    b.dispatchEvent(new wa("select"))
                }
            };
        this.__inputControl.click();
        return !0
    },
    load: function() {
        var a = this,
            b = this.__inputControl.files[0],
            c = new FileReader;
        c.onload = function(b) {
            a.data = Td.fromArrayBuffer(b.target.result);
            a.openFileDialog_onComplete()
        };
        c.onerror = function(b) {
            a.dispatchEvent(new Rg("ioError"))
        };
        c.readAsArrayBuffer(b)
    },
    openFileDialog_onComplete: function() {
        this.dispatchEvent(new wa("complete"))
    },
    get_modificationDate: function() {
        return this.modificationDate
    },
    __class__: Gf,
    __properties__: {
        get_modificationDate: "get_modificationDate"
    }
});
var Rk = function() {
    oa.call(this)
};
g["openfl.net.NetConnection"] = Rk;
Rk.__name__ = "openfl.net.NetConnection";
Rk.__super__ = oa;
Rk.prototype = v(oa.prototype, {
    __class__: Rk
});
var Sk = function(a,
    b) {
    oa.call(this);
    this.__connection = a;
    this.__soundTransform = new dg;
    this.__video = window.document.createElement("video");
    this.__video.setAttribute("playsinline", "");
    this.__video.setAttribute("webkit-playsinline", "");
    this.__video.setAttribute("crossorigin", "anonymous");
    this.__video.addEventListener("error", l(this, this.video_onError), !1);
    this.__video.addEventListener("waiting", l(this, this.video_onWaiting), !1);
    this.__video.addEventListener("ended", l(this, this.video_onEnd), !1);
    this.__video.addEventListener("pause",
        l(this, this.video_onPause), !1);
    this.__video.addEventListener("seeking", l(this, this.video_onSeeking), !1);
    this.__video.addEventListener("playing", l(this, this.video_onPlaying), !1);
    this.__video.addEventListener("timeupdate", l(this, this.video_onTimeUpdate), !1);
    this.__video.addEventListener("loadstart", l(this, this.video_onLoadStart), !1);
    this.__video.addEventListener("stalled", l(this, this.video_onStalled), !1);
    this.__video.addEventListener("durationchanged", l(this, this.video_onDurationChanged), !1);
    this.__video.addEventListener("canplay",
        l(this, this.video_onCanPlay), !1);
    this.__video.addEventListener("canplaythrough", l(this, this.video_onCanPlayThrough), !1);
    this.__video.addEventListener("loadedmetadata", l(this, this.video_onLoadMetaData), !1)
};
g["openfl.net.NetStream"] = Sk;
Sk.__name__ = "openfl.net.NetStream";
Sk.__super__ = oa;
Sk.prototype = v(oa.prototype, {
    __dispatchStatus: function(a) {
        a = new Dj("netStatus", !1, !1, {
            code: a
        });
        this.__connection.dispatchEvent(a);
        this.dispatchEvent(a)
    },
    __playStatus: function(a) {
        if (null != this.__video && null != this.client) try {
            var b =
                this.client.onPlayStatus;
            b({
                code: a,
                duration: this.__video.duration,
                position: this.__video.currentTime,
                speed: this.__video.playbackRate,
                start: this.__video.startTime
            })
        } catch (c) {
            Ta.lastError = c
        }
    },
    video_onCanPlay: function(a) {
        this.__playStatus("NetStream.Play.canplay")
    },
    video_onCanPlayThrough: function(a) {
        this.__playStatus("NetStream.Play.canplaythrough")
    },
    video_onDurationChanged: function(a) {
        this.__playStatus("NetStream.Play.durationchanged")
    },
    video_onEnd: function(a) {
        this.__dispatchStatus("NetStream.Play.Stop");
        this.__dispatchStatus("NetStream.Play.Complete");
        this.__playStatus("NetStream.Play.Complete")
    },
    video_onError: function(a) {
        this.__dispatchStatus("NetStream.Play.Stop");
        this.__playStatus("NetStream.Play.error")
    },
    video_onLoadMetaData: function(a) {
        if (null != this.__video && null != this.client) try {
            var b = this.client.onMetaData;
            b({
                width: this.__video.videoWidth,
                height: this.__video.videoHeight,
                duration: this.__video.duration
            })
        } catch (c) {
            Ta.lastError = c
        }
    },
    video_onLoadStart: function(a) {
        this.__playStatus("NetStream.Play.loadstart")
    },
    video_onPause: function(a) {
        this.__playStatus("NetStream.Play.pause")
    },
    video_onPlaying: function(a) {
        this.__dispatchStatus("NetStream.Play.Start");
        this.__playStatus("NetStream.Play.playing")
    },
    video_onSeeking: function(a) {
        this.__playStatus("NetStream.Play.seeking");
        this.__dispatchStatus("NetStream.Seek.Complete")
    },
    video_onStalled: function(a) {
        this.__playStatus("NetStream.Play.stalled")
    },
    video_onTimeUpdate: function(a) {
        null != this.__video && (this.time = this.__video.currentTime, this.__playStatus("NetStream.Play.timeupdate"))
    },
    video_onWaiting: function(a) {
        this.__playStatus("NetStream.Play.waiting")
    },
    __class__: Sk
});
var $c = function() {
    oa.call(this);
    this.client = this;
    this.objectEncoding = $c.defaultObjectEncoding
};
g["openfl.net.SharedObject"] = $c;
$c.__name__ = "openfl.net.SharedObject";
$c.getLocal = function(a, b, c) {
    c = " ~%&\\;:\"',<>?#".split("");
    var d = !0;
    if (null == a || "" == a) d = !1;
    else
        for (var f = 0; f < c.length;) {
            var h = c[f];
            ++f;
            if (-1 < a.indexOf(h)) {
                d = !1;
                break
            }
        }
    if (!d) throw new Vb("Error #2134: Cannot create SharedObject.");
    null == $c.__sharedObjects &&
        ($c.__sharedObjects = new Qa, null != A.current && A.current.onExit.add($c.application_onExit));
    c = b + "/" + a;
    if (!Object.prototype.hasOwnProperty.call($c.__sharedObjects.h, c)) {
        d = null;
        try {
            var k = qf.getLocalStorage();
            null == b && (null != k && (d = k.getItem(window.location.href + ":" + a), k.removeItem(window.location.href + ":" + a)), b = window.location.pathname);
            null != k && null == d && (d = k.getItem(b + ":" + a))
        } catch (p) {
            Ta.lastError = p
        }
        k = new $c;
        k.data = {};
        k.__localPath = b;
        k.__name = a;
        if (null != d && "" != d) try {
            var g = new pd(d);
            g.setResolver({
                resolveEnum: w.resolveEnum,
                resolveClass: $c.__resolveClass
            });
            k.data = g.unserialize()
        } catch (p) {
            Ta.lastError = p
        }
        $c.__sharedObjects.h[c] = k
    }
    return $c.__sharedObjects.h[c]
};
$c.__resolveClass = function(a) {
    return null != a ? (O.startsWith(a, "neash.") && (a = O.replace(a, "neash.", "openfl.")), O.startsWith(a, "native.") && (a = O.replace(a, "native.", "openfl.")), O.startsWith(a, "flash.") && (a = O.replace(a, "flash.", "openfl.")), O.startsWith(a, "openfl._v2.") && (a = O.replace(a, "openfl._v2.", "openfl.")), O.startsWith(a, "openfl._legacy.") && (a = O.replace(a, "openfl._legacy.",
        "openfl.")), g[a]) : null
};
$c.application_onExit = function(a) {
    a = $c.__sharedObjects.h;
    for (var b = Object.keys(a), c = b.length, d = 0; d < c;) a[b[d++]].flush()
};
$c.__super__ = oa;
$c.prototype = v(oa.prototype, {
    flush: function(a) {
        if (0 == ya.fields(this.data).length) return 0;
        a = Bd.run(this.data);
        try {
            var b = qf.getLocalStorage();
            null != b && (b.removeItem(this.__localPath + ":" + this.__name), b.setItem(this.__localPath + ":" + this.__name, a))
        } catch (c) {
            return Ta.lastError = c, 1
        }
        return 0
    },
    __class__: $c
});
var Ai = function(a) {
    oa.call(this);
    this.bytesTotal =
        this.bytesLoaded = 0;
    this.dataFormat = 1;
    null != a && this.load(a)
};
g["openfl.net.URLLoader"] = Ai;
Ai.__name__ = "openfl.net.URLLoader";
Ai.__super__ = oa;
Ai.prototype = v(oa.prototype, {
    load: function(a) {
        var b = this,
            c = new wa("open");
        this.dispatchEvent(c);
        0 == this.dataFormat ? (c = new Wi, this.__prepareRequest(c, a), c.load().onProgress(l(this, this.httpRequest_onProgress)).onError(l(this, this.httpRequest_onError)).onComplete(function(a) {
                b.__dispatchResponseStatus();
                b.__dispatchStatus();
                b.data = a;
                a = new wa("complete");
                b.dispatchEvent(a)
            })) :
            (c = new Gg, this.__prepareRequest(c, a), c.load().onProgress(l(this, this.httpRequest_onProgress)).onError(l(this, this.httpRequest_onError)).onComplete(function(a) {
                b.__dispatchResponseStatus();
                b.__dispatchStatus();
                b.data = 2 == b.dataFormat ? Dl._new(a) : a;
                a = new wa("complete");
                b.dispatchEvent(a)
            }))
    },
    __dispatchResponseStatus: function() {
        var a = new Ih("httpResponseStatus", !1, !1, this.__httpRequest.responseStatus);
        a.responseURL = this.__httpRequest.uri;
        var b = [];
        if (this.__httpRequest.enableResponseHeaders && null != this.__httpRequest.responseHeaders)
            for (var c =
                    0, d = this.__httpRequest.responseHeaders; c < d.length;) {
                var f = d[c];
                ++c;
                b.push(new Tk(f.name, f.value))
            }
        a.responseHeaders = b;
        this.dispatchEvent(a)
    },
    __dispatchStatus: function() {
        var a = new Ih("httpStatus", !1, !1, this.__httpRequest.responseStatus);
        this.dispatchEvent(a)
    },
    __prepareRequest: function(a, b) {
        this.__httpRequest = a;
        this.__httpRequest.uri = b.url;
        this.__httpRequest.method = b.method;
        if (null != b.data)
            if (w.typeof(b.data) == J.TObject) {
                var c = ya.fields(b.data);
                for (a = 0; a < c.length;) {
                    var d = c[a];
                    ++a;
                    this.__httpRequest.formData.h[d] =
                        ya.field(b.data, d)
                }
            } else this.__httpRequest.data = b.data instanceof zb ? b.data : zb.ofString(H.string(b.data));
        this.__httpRequest.contentType = b.contentType;
        if (null != b.requestHeaders)
            for (a = 0, c = b.requestHeaders; a < c.length;) d = c[a], ++a, this.__httpRequest.headers.push(new Oi(d.name, d.value));
        this.__httpRequest.followRedirects = b.followRedirects;
        this.__httpRequest.timeout = b.idleTimeout | 0;
        this.__httpRequest.manageCookies = b.manageCookies;
        this.__httpRequest.withCredentials = b.withCredentials;
        this.__httpRequest.userAgent =
            b.userAgent;
        this.__httpRequest.enableResponseHeaders = !0
    },
    httpRequest_onError: function(a) {
        this.__dispatchResponseStatus();
        this.__dispatchStatus();
        this.__httpRequest instanceof tf ? this.data = this.__httpRequest.responseData : this.__httpRequest instanceof Gg && (this.data = this.__httpRequest.responseData);
        var b = 403 == a ? new Fj("securityError") : new Rg("ioError");
        b.text = H.string(a);
        this.dispatchEvent(b)
    },
    httpRequest_onProgress: function(a, b) {
        var c = new Wf("progress");
        c.bytesLoaded = a;
        c.bytesTotal = b;
        this.dispatchEvent(c)
    },
    __class__: Ai
});
var If = function(a) {
    this.withCredentials = !1;
    null != a && (this.url = a);
    this.contentType = null;
    this.followRedirects = xf.followRedirects;
    this.idleTimeout = 0 < xf.idleTimeout ? xf.idleTimeout : 3E4;
    this.manageCookies = xf.manageCookies;
    this.method = "GET";
    this.requestHeaders = [];
    this.userAgent = xf.userAgent
};
g["openfl.net.URLRequest"] = If;
If.__name__ = "openfl.net.URLRequest";
If.prototype = {
    __class__: If
};
var xf = function() {};
g["openfl.net.URLRequestDefaults"] = xf;
xf.__name__ = "openfl.net.URLRequestDefaults";
var Tk =
    function(a, b) {
        null == b && (b = "");
        null == a && (a = "");
        this.name = a;
        this.value = b
    };
g["openfl.net.URLRequestHeader"] = Tk;
Tk.__name__ = "openfl.net.URLRequestHeader";
Tk.prototype = {
    __class__: Tk
};
var Dl = {
        _new: function(a) {
            var b = {};
            null != a && Dl.decode(b, a);
            return b
        },
        decode: function(a, b) {
            for (var c = ya.fields(a), d = 0; d < c.length;) {
                var f = c[d];
                ++d;
                ya.deleteField(a, f)
            }
            c = b.split(";").join("&").split("&");
            for (d = 0; d < c.length;)
                if (f = c[d], ++d, b = f.indexOf("="), 0 < b) {
                    var h = N.substr(f, 0, b);
                    h = decodeURIComponent(h.split("+").join(" "));
                    f = N.substr(f, b + 1, null);
                    a[h] = decodeURIComponent(f.split("+").join(" "))
                } else 0 != b && (a[decodeURIComponent(f.split("+").join(" "))] = "")
        }
    },
    Jg = function(a) {
        this.parentDomain = null != a ? a : Jg.currentDomain
    };
g["openfl.system.ApplicationDomain"] = Jg;
Jg.__name__ = "openfl.system.ApplicationDomain";
Jg.prototype = {
    __class__: Jg
};
var Vj = function() {};
g["openfl.system.Capabilities"] = Vj;
Vj.__name__ = "openfl.system.Capabilities";
Vj.__properties__ = {
    get_screenDPI: "get_screenDPI"
};
Vj.get_screenDPI = function() {
    var a = null != Dc.application ?
        Dc.application.__window : null,
        b = 72;
    null != a && (b *= a.__scale);
    return b
};
var Uk = function() {
    oa.call(this);
    this.clear()
};
g["openfl.text.StyleSheet"] = Uk;
Uk.__name__ = "openfl.text.StyleSheet";
Uk.__super__ = oa;
Uk.prototype = v(oa.prototype, {
    clear: function() {
        this.__styleNamesDirty = !1;
        this.__styleNames = null;
        this.__styles = new Qa
    },
    __applyStyle: function(a, b) {
        a = a.toLowerCase();
        Object.prototype.hasOwnProperty.call(this.__styles.h, a) && this.__applyStyleObject(this.__styles.h[a], b)
    },
    __applyStyleObject: function(a, b) {
        if (null !=
            a) {
            var c = new ja("[0-9A-Fa-f]+", ""),
                d = new ja("[0-9]+", "");
            var f = a;
            null != f && Object.prototype.hasOwnProperty.call(f, "color") ? (f = Sd.__get(a, "color"), f = c.match(null == f ? null : H.string(f))) : f = !1;
            f && (b.color = H.parseInt("0x" + c.matched(0)));
            f = a;
            null != f && Object.prototype.hasOwnProperty.call(f, "fontFamily") && (f = Sd.__get(a, "fontFamily"), b.font = this.__parseFont(null == f ? null : H.string(f)));
            f = a;
            null != f && Object.prototype.hasOwnProperty.call(f, "fontSize") ? (f = Sd.__get(a, "fontSize"), f = d.match(null == f ? null : H.string(f))) :
                f = !1;
            f && (b.size = H.parseInt(d.matched(0)));
            f = a;
            if (null != f && Object.prototype.hasOwnProperty.call(f, "fontStyle")) switch (Sd.__get(a, "fontStyle")) {
                case "italic":
                    b.italic = !0;
                    break;
                case "normal":
                    b.italic = !1
            }
            f = a;
            if (null != f && Object.prototype.hasOwnProperty.call(f, "fontWeight")) switch (Sd.__get(a, "fontWeight")) {
                case "bold":
                    b.bold = !0;
                    break;
                case "normal":
                    b.bold = !1
            }
            f = a;
            null != f && Object.prototype.hasOwnProperty.call(f, "leading") ? (f = Sd.__get(a, "leading"), f = d.match(null == f ? null : H.string(f))) : f = !1;
            f && (b.leading = H.parseInt(d.matched(0)));
            f = a;
            null != f && Object.prototype.hasOwnProperty.call(f, "letterSpacing") ? (f = Sd.__get(a, "letterSpacing"), f = d.match(null == f ? null : H.string(f))) : f = !1;
            f && (b.letterSpacing = parseFloat(d.matched(0)));
            f = a;
            null != f && Object.prototype.hasOwnProperty.call(f, "marginLeft") ? (f = Sd.__get(a, "marginLeft"), f = d.match(null == f ? null : H.string(f))) : f = !1;
            f && (b.leftMargin = H.parseInt(d.matched(0)));
            f = a;
            null != f && Object.prototype.hasOwnProperty.call(f, "marginRight") ? (f = Sd.__get(a, "marginRight"), f = d.match(null == f ? null : H.string(f))) : f = !1;
            f && (b.rightMargin = H.parseInt(d.matched(0)));
            f = a;
            if (null != f && Object.prototype.hasOwnProperty.call(f, "textAlign")) switch (Sd.__get(a, "textAlign")) {
                case "center":
                    b.align = 0;
                    break;
                case "justify":
                    b.align = 2;
                    break;
                case "left":
                    b.align = 3;
                    break;
                case "right":
                    b.align = 4
            }
            f = a;
            if (null != f && Object.prototype.hasOwnProperty.call(f, "textDecoration")) switch (Sd.__get(a, "textDecoration")) {
                case "none":
                    b.underline = !1;
                    break;
                case "underline":
                    b.underline = !0
            }
            f = a;
            null != f && Object.prototype.hasOwnProperty.call(f, "textIndent") ? (f =
                Sd.__get(a, "textIndent"), f = d.match(null == f ? null : H.string(f))) : f = !1;
            f && (b.blockIndent = H.parseInt(d.matched(0)))
        }
    },
    __parseFont: function(a) {
        if (null == a) return null; - 1 < a.indexOf(",") && (a = N.substr(a, 0, a.indexOf(",")));
        switch (a) {
            case "mono":
                return "_typewriter";
            case "sans-serif":
                return "_sans";
            case "serif":
                return "_serif";
            default:
                return a
        }
    },
    __class__: Uk
});
var we = function(a, b, c, d, f, h, k, g, q, u, r, m, l) {
    this.font = a;
    this.size = b;
    this.color = c;
    this.bold = d;
    this.italic = f;
    this.underline = h;
    this.url = k;
    this.target = g;
    this.align =
        q;
    this.leftMargin = u;
    this.rightMargin = r;
    this.indent = m;
    this.leading = l
};
g["openfl.text.TextFormat"] = we;
we.__name__ = "openfl.text.TextFormat";
we.prototype = {
    clone: function() {
        var a = new we(this.font, this.size, this.color, this.bold, this.italic, this.underline, this.url, this.target);
        a.align = this.align;
        a.leftMargin = this.leftMargin;
        a.rightMargin = this.rightMargin;
        a.indent = this.indent;
        a.leading = this.leading;
        a.blockIndent = this.blockIndent;
        a.bullet = this.bullet;
        a.kerning = this.kerning;
        a.letterSpacing = this.letterSpacing;
        a.tabStops = this.tabStops;
        a.__ascent = this.__ascent;
        a.__descent = this.__descent;
        a.__cacheKey = this.__toCacheKey();
        return a
    },
    __merge: function(a) {
        null != a.font && (this.font = a.font);
        null != a.size && (this.size = a.size);
        null != a.color && (this.color = a.color);
        null != a.bold && (this.bold = a.bold);
        null != a.italic && (this.italic = a.italic);
        null != a.underline && (this.underline = a.underline);
        null != a.url && (this.url = a.url);
        null != a.target && (this.target = a.target);
        null != a.align && (this.align = a.align);
        null != a.leftMargin && (this.leftMargin =
            a.leftMargin);
        null != a.rightMargin && (this.rightMargin = a.rightMargin);
        null != a.indent && (this.indent = a.indent);
        null != a.leading && (this.leading = a.leading);
        null != a.blockIndent && (this.blockIndent = a.blockIndent);
        null != a.bullet && (this.bullet = a.bullet);
        null != a.kerning && (this.kerning = a.kerning);
        null != a.letterSpacing && (this.letterSpacing = a.letterSpacing);
        null != a.tabStops && (this.tabStops = a.tabStops);
        null != a.__ascent && (this.__ascent = a.__ascent);
        null != a.__descent && (this.__descent = a.__descent);
        this.__toCacheKey()
    },
    __toCacheKey: function() {
        return this.__cacheKey = "" + this.font + this.size + H.string(this.bold) + H.string(this.italic)
    },
    __class__: we
};
var Pl = {
        fromString: function(a) {
            switch (a) {
                case "center":
                    return 0;
                case "end":
                    return 1;
                case "justify":
                    return 2;
                case "left":
                    return 3;
                case "right":
                    return 4;
                case "start":
                    return 5;
                default:
                    return null
            }
        }
    },
    Jh = function(a, b) {
        this.__collisions = [];
        this.__wordMap = new mc;
        this.set(a, b)
    };
g["openfl.text._internal.CacheMeasurement"] = Jh;
Jh.__name__ = "openfl.text._internal.CacheMeasurement";
Jh.prototype = {
    set: function(a, b) {
        this.__addCollision(a, b)
    },
    get: function(a) {
        return 1 < this.__collisions.length ? this.__wordMap.h[this.__collisions.indexOf(a)] : this.__wordMap.h[0]
    },
    __addCollision: function(a, b) {
        if (!this.exists(a)) {
            var c = this.__wordMap;
            a = this.__collisions.push(a) - 1;
            c.h[a] = b
        }
    },
    exists: function(a) {
        return 0 == this.__collisions.length ? !1 : -1 < this.__collisions.indexOf(a)
    },
    __class__: Jh
};
var La = function() {};
g["openfl.text._internal.HTMLParser"] = La;
La.__name__ = "openfl.text._internal.HTMLParser";
La.parse = function(a,
    b, c, d, f) {
    a = b ? a.replace(La.__regexBreakTag.r, "\n") : a.replace(La.__regexBreakTag.r, "");
    a = a.replace(La.__regexEntityNbsp.r, " ");
    a = La.__regexCharEntity.map(a, function(a) {
        var b = a.matched(1),
            c = a.matched(2);
        return null != b && (b = H.parseInt(b), null != b) ? String.fromCodePoint(b) : null != c && (c = H.parseInt("0" + c), null != c) ? String.fromCodePoint(c) : a.matched(0)
    });
    var h = a.split("<");
    if (1 == h.length) {
        a = La.__htmlUnescape(a.replace(La.__regexHTMLTag.r, ""));
        if (1 < f.get_length()) {
            var k = f.get_length() - 1;
            f.__tempIndex = 1;
            for (var g =
                    0, q = []; g < q.length;) {
                var u = q[g++];
                f.insertAt(f.__tempIndex, u);
                f.__tempIndex++
            }
            f.splice(f.__tempIndex, k)
        }
        b = f.get(0);
        b.format = d;
        b.start = 0;
        b.end = a.length
    } else {
        k = f.get_length();
        g = f.__tempIndex = 0;
        for (q = []; g < q.length;) u = q[g++], f.insertAt(f.__tempIndex, u), f.__tempIndex++;
        f.splice(f.__tempIndex, k);
        a = "";
        k = [d.clone()];
        g = [];
        u = !1;
        for (q = 0; q < h.length;) {
            var r = h[q];
            ++q;
            if ("" != r) {
                var m = "/" == N.substr(r, 0, 1),
                    l = r.indexOf(">"),
                    x = l + 1,
                    D = r.indexOf(" ");
                D = r.substring(m ? 1 : 0, -1 < D && D < l ? D : l).toLowerCase();
                if (m) 0 != g.length && D ==
                    g[g.length - 1] && (g.pop(), k.pop(), m = k[k.length - 1].clone(), ("p" == D || "li" == D) && 0 < f.get_length() && (b && (a += "\n"), u = !0), x < r.length && (u = La.__htmlUnescape(N.substr(r, x, null)), f.push(new od(m, a.length, a.length + u.length)), a += u, u = !1));
                else if (m = k[k.length - 1].clone(), -1 < l) {
                    null != c && (c.__applyStyle(D, m), La.__regexClass.match(r) && (c.__applyStyle("." + La.__getAttributeMatch(La.__regexClass), m), c.__applyStyle(D + "." + La.__getAttributeMatch(La.__regexClass), m)));
                    switch (D) {
                        case "a":
                            null != c && c.__applyStyle("a:link", m);
                            La.__regexHref.match(r) && (m.url = La.__getAttributeMatch(La.__regexHref));
                            break;
                        case "b":
                            m.bold = !0;
                            break;
                        case "em":
                        case "i":
                            m.italic = !0;
                            break;
                        case "font":
                            La.__regexFace.match(r) && (m.font = La.__getAttributeMatch(La.__regexFace));
                            La.__regexColor.match(r) && (m.color = H.parseInt("0x" + La.__getAttributeMatch(La.__regexColor)));
                            if (La.__regexSize.match(r)) {
                                l = La.__getAttributeMatch(La.__regexSize);
                                var w = N.cca(l, 0);
                                m.size = 43 == w || 45 == w ? (2 <= k.length ? k[k.length - 2] : d).size + H.parseInt(l) : H.parseInt(l)
                            }
                            break;
                        case "li":
                            0 <
                                f.get_length() && !u && (a += "\n");
                            l = m.clone();
                            l.underline = !1;
                            f.push(new od(l, a.length, a.length + 2));
                            a += "\u2022 ";
                            break;
                        case "p":
                            0 < f.get_length() && !u && (a += "\n");
                            La.__regexAlign.match(r) && (l = La.__getAttributeMatch(La.__regexAlign).toLowerCase(), m.align = Pl.fromString(l));
                            break;
                        case "textformat":
                            La.__regexBlockIndent.match(r) && (m.blockIndent = H.parseInt(La.__getAttributeMatch(La.__regexBlockIndent)));
                            La.__regexIndent.match(r) && (m.indent = H.parseInt(La.__getAttributeMatch(La.__regexIndent)));
                            La.__regexLeading.match(r) &&
                                (m.leading = H.parseInt(La.__getAttributeMatch(La.__regexLeading)));
                            La.__regexLeftMargin.match(r) && (m.leftMargin = H.parseInt(La.__getAttributeMatch(La.__regexLeftMargin)));
                            La.__regexRightMargin.match(r) && (m.rightMargin = H.parseInt(La.__getAttributeMatch(La.__regexRightMargin)));
                            if (La.__regexTabStops.match(r)) {
                                l = La.__getAttributeMatch(La.__regexTabStops).split(" ");
                                w = [];
                                for (var J = 0; J < l.length;) {
                                    var y = l[J];
                                    ++J;
                                    w.push(H.parseInt(y))
                                }
                                m.tabStops = w
                            }
                            break;
                        case "u":
                            m.underline = !0
                    }
                    k.push(m);
                    g.push(D);
                    x < r.length &&
                        (u = La.__htmlUnescape(r.substring(x)), f.push(new od(m, a.length, a.length + u.length)), a += u, u = !1)
                } else u = La.__htmlUnescape(r), f.push(new od(m, a.length, a.length + u.length)), a += u, u = !1
            }
        }
        0 == f.get_length() && f.push(new od(k[0], 0, 0))
    }
    return a
};
La.__getAttributeMatch = function(a) {
    return null != a.matched(2) ? a.matched(2) : a.matched(3)
};
La.__htmlUnescape = function(a) {
    a = a.replace(La.__regexEntityApos.r, "'");
    return O.htmlUnescape(a)
};
var Ug = function() {
    this.__shortWordMap = new Qa;
    this.__longWordMap = new Qa
};
g["openfl.text._internal.ShapeCache"] =
    Ug;
Ug.__name__ = "openfl.text._internal.ShapeCache";
Ug.hashFunction = function(a) {
    for (var b = 0, c, d = 0, f = a.length; d < f;) c = d++, c = N.cca(a, c), b = (b << 5) - b + c, b |= 0;
    return b
};
Ug.prototype = {
    cache: function(a, b, c) {
        a = a.format.__cacheKey;
        return 15 < c.length ? this.__cacheLongWord(c, a, b) : this.__cacheShortWord(c, a, b)
    },
    __cacheShortWord: function(a, b, c) {
        if (Object.prototype.hasOwnProperty.call(this.__shortWordMap.h, b)) {
            var d = this.__shortWordMap.h[b];
            if (Object.prototype.hasOwnProperty.call(d.h, a)) return d.h[a];
            var f = c();
            d.h[a] =
                f
        } else d = new Qa, f = c(), d.h[a] = f, this.__shortWordMap.h[b] = d;
        return c()
    },
    __cacheLongWord: function(a, b, c) {
        var d = Ug.hashFunction(a);
        if (Object.prototype.hasOwnProperty.call(this.__longWordMap.h, b)) {
            var f = this.__longWordMap.h[b];
            if (f.h.hasOwnProperty(d)) {
                var h = f.h[d];
                if (h.exists(a)) return h.get(a);
                h.set(a, c())
            } else h = new Jh(a, c()), f.h[d] = h
        } else f = new mc, h = new Jh(a, c()), h.hash = d, f.h[d] = h, this.__longWordMap.h[b] = f;
        return c()
    },
    __class__: Ug
};
var Nb = function(a) {
    this.__shapeCache = new Ug;
    this.textField = a;
    this.height =
        this.width = 100;
    this.set_text("");
    this.bounds = new na(0, 0, 0, 0);
    this.textBounds = new na(0, 0, 0, 0);
    this.type = 0;
    this.autoSize = 2;
    this.embedFonts = !1;
    this.selectable = !0;
    this.borderColor = 0;
    this.border = !1;
    this.backgroundColor = 16777215;
    this.background = !1;
    this.gridFitType = 1;
    this.maxChars = 0;
    this.multiline = !1;
    this.numLines = 1;
    this.scrollH = this.sharpness = 0;
    this.set_scrollV(1);
    this.wordWrap = !1;
    this.lineAscents = la.toFloatVector(null);
    this.lineBreaks = la.toIntVector(null);
    this.lineDescents = la.toFloatVector(null);
    this.lineLeadings =
        la.toFloatVector(null);
    this.lineHeights = la.toFloatVector(null);
    this.lineWidths = la.toFloatVector(null);
    this.layoutGroups = la.toObjectVector(null);
    this.textFormatRanges = la.toObjectVector(null);
    null == Nb.__context && (Nb.__context = window.document.createElement("canvas").getContext("2d"))
};
g["openfl.text._internal.TextEngine"] = Nb;
Nb.__name__ = "openfl.text._internal.TextEngine";
Nb.findFont = function(a) {
    return ha.__fontByName.h[a]
};
Nb.findFontVariant = function(a) {
    var b = a.font,
        c = a.bold;
    a = a.italic;
    null == b && (b = "_serif");
    var d = O.replace(O.replace(b, " Normal", ""), " Regular", "");
    return c && a && Object.prototype.hasOwnProperty.call(ha.__fontByName.h, d + " Bold Italic") ? Nb.findFont(d + " Bold Italic") : c && Object.prototype.hasOwnProperty.call(ha.__fontByName.h, d + " Bold") ? Nb.findFont(d + " Bold") : a && Object.prototype.hasOwnProperty.call(ha.__fontByName.h, d + " Italic") ? Nb.findFont(d + " Italic") : Nb.findFont(b)
};
Nb.getFormatHeight = function(a) {
    var b = Nb.getFont(a);
    Nb.__context.font = b;
    b = Nb.getFontInstance(a);
    if (null != a.__ascent) {
        var c = a.size *
            a.__ascent;
        b = a.size * a.__descent
    } else null != b && 0 != b.unitsPerEM ? (c = b.ascender / b.unitsPerEM * a.size, b = Math.abs(b.descender / b.unitsPerEM * a.size)) : (c = a.size, b = .185 * a.size);
    return c + b + a.leading
};
Nb.getFont = function(a) {
    var b = a.font,
        c = a.bold,
        d = a.italic;
    null == b && (b = "_serif");
    var f = O.replace(O.replace(b, " Normal", ""), " Regular", "");
    c && d && Object.prototype.hasOwnProperty.call(ha.__fontByName.h, f + " Bold Italic") ? (b = f + " Bold Italic", d = c = !1) : c && Object.prototype.hasOwnProperty.call(ha.__fontByName.h, f + " Bold") ? (b =
        f + " Bold", c = !1) : d && Object.prototype.hasOwnProperty.call(ha.__fontByName.h, f + " Italic") ? (b = f + " Italic", d = !1) : (c && (-1 < b.indexOf(" Bold ") || O.endsWith(b, " Bold")) && (c = !1), d && (-1 < b.indexOf(" Italic ") || O.endsWith(b, " Italic")) && (d = !1));
    c = (d ? "italic " : "normal ") + "normal " + (c ? "bold " : "normal ");
    c += a.size + "px";
    c += "/" + (a.size + 3) + "px ";
    switch (b) {
        case "_sans":
            a = "sans-serif";
            break;
        case "_serif":
            a = "serif";
            break;
        case "_typewriter":
            a = "monospace";
            break;
        default:
            a = "'" + b.replace(/^[\s'"]+(.*)[\s'"]+$/, "$1") + "'"
    }
    return c +=
        "" + a
};
Nb.getFontInstance = function(a) {
    return Nb.findFontVariant(a)
};
Nb.prototype = {
    createRestrictRegexp: function(a) {
        var b = "",
            c = !1;
        a = (new ja("\\^([^\\^]+)", "gu")).map(a, function(a) {
            if (c) return c = !c, a.matched(1);
            b += a.matched(1);
            c = !c;
            return ""
        });
        var d = [];
        0 < a.length && d.push("[^" + a + "]");
        0 < b.length && d.push("[" + b + "]");
        return new ja("(" + d.join("|") + ")", "g")
    },
    getBounds: function() {
        var a = this.border ? 1 : 0;
        this.bounds.width = this.width + a;
        this.bounds.height = this.height + a;
        a = this.width;
        for (var b = this.height, c = this.layoutGroups.get_length() -
                1, d = 0, f = this.layoutGroups.get_length(); d < f;) {
            var h = d++,
                k = this.layoutGroups.get(h);
            if (h != c || k.startIndex != k.endIndex || 1 == this.type) k.offsetX < a && (a = k.offsetX), k.offsetY < b && (b = k.offsetY)
        }
        a >= this.width && (a = 2);
        b >= this.height && (b = 2);
        this.textBounds.setTo(Math.max(a - 2, 0), Math.max(b - 2, 0), Math.min(this.textWidth + 4, this.bounds.width), Math.min(1.185 * this.textHeight + 4, this.bounds.height))
    },
    getLineBreaks: function() {
        this.lineBreaks.set_length(0);
        for (var a = -1, b; a < this.text.length;)
            if (b = this.text.indexOf("\n", a +
                    1), a = this.text.indexOf("\r", a + 1), a = -1 == a ? b : -1 == b ? a : a < b ? a : b, -1 < a) this.lineBreaks.push(a);
            else break
    },
    getLineMeasurements: function() {
        this.lineAscents.set_length(0);
        this.lineDescents.set_length(0);
        this.lineLeadings.set_length(0);
        this.lineHeights.set_length(0);
        this.lineWidths.set_length(0);
        var a = 0,
            b = 0,
            c = null,
            d = 0,
            f = 0;
        this.textHeight = this.textWidth = 0;
        this.numLines = 1;
        this.maxScrollH = 0;
        for (var h = this.layoutGroups.get_length() - 1, k = 0, g = this.layoutGroups.get_length(); k < g;) {
            var q = k++;
            var u = this.layoutGroups.get(q);
            if (q != h || u.startIndex != u.endIndex || 1 == this.type) {
                for (; u.lineIndex > this.numLines - 1;) this.lineAscents.push(a), this.lineDescents.push(b), this.lineLeadings.push(null != c ? c : 0), this.lineHeights.push(d), this.lineWidths.push(f), b = a = 0, c = null, f = d = 0, this.numLines++;
                a = Math.max(a, u.ascent);
                b = Math.max(b, u.descent);
                c = null == c ? u.leading : Math.max(c, u.leading) | 0;
                d = Math.max(d, u.height);
                f = u.offsetX - 2 + u.width;
                f > this.textWidth && (this.textWidth = f);
                q = Math.ceil(u.offsetY - 2 + u.ascent + u.descent);
                q > this.textHeight && (this.textHeight =
                    q)
            }
        }
        0 == this.textHeight && null != this.textField && 1 == this.type && (a = this.textField.__textFormat, b = Nb.getFontInstance(a), null != a.__ascent ? (h = a.size * a.__ascent, k = a.size * a.__descent) : null != b && 0 != b.unitsPerEM ? (h = b.ascender / b.unitsPerEM * a.size, k = Math.abs(b.descender / b.unitsPerEM * a.size)) : (h = a.size, k = .185 * a.size), c = a.leading, a = h, b = k, this.textHeight = q = Math.ceil(h + k));
        this.lineAscents.push(a);
        this.lineDescents.push(b);
        this.lineLeadings.push(null != c ? c : 0);
        this.lineHeights.push(d);
        this.lineWidths.push(f);
        1 == this.numLines &&
            0 < c && (this.textHeight += c);
        if (2 != this.autoSize) switch (this.autoSize) {
            case 0:
            case 1:
            case 3:
                this.wordWrap || (this.width = this.textWidth + 4), this.height = this.textHeight + 4, this.bottomScrollV = this.numLines
        }
        this.maxScrollH = this.textWidth > this.width - 4 ? this.textWidth - this.width + 4 | 0 : 0;
        this.scrollH > this.maxScrollH && (this.scrollH = this.maxScrollH)
    },
    getLayoutGroups: function() {
        var a = this;
        this.layoutGroups.set_length(0);
        if (null != this.text && "" != this.text) {
            var b = -1,
                c = null,
                d = null,
                f = sc.__defaultTextFormat.clone(),
                h = 0,
                k =
                0,
                g = 0,
                q = 0,
                u = 3,
                r = 0,
                m = 0,
                l = 0,
                x = 0,
                D = !0,
                w = null,
                J = null,
                y = 0,
                z = 0,
                C = 0,
                t = -2,
                v = -1,
                G = this.text.indexOf(" "),
                F = 0,
                I = F < this.lineBreaks.get_length() ? this.lineBreaks.get(F) : -1,
                K = 0,
                eb = 0,
                H = 0,
                O = 0,
                lb = function(b, d, h) {
                    var k = function() {
                        var c = [];
                        if (null == a.__useIntAdvances) {
                            var f = new ja("Trident/7.0", "");
                            a.__useIntAdvances = f.match(E.navigator.userAgent)
                        }
                        if (a.__useIntAdvances) {
                            var k = 0;
                            f = d;
                            for (var g = h; f < g;) {
                                var m = f++;
                                m = a.measureText(b.substring(d, m + 1));
                                c.push(m - k);
                                k = m
                            }
                        } else
                            for (f = d, g = h; f < g;) m = f++, m < b.length - 1 ? (k = a.measureText(b.charAt(m +
                                1)), k = a.measureText(N.substr(b, m, 2)) - k) : k = Nb.__context.measureText(b.charAt(m)).width, c.push(k);
                        return c
                    };
                    return 2 == f.align ? k() : a.__shapeCache.cache(c, k, b.substring(d, h))
                },
                Va = function(a) {
                    for (var b = 0, c = 0; c < a.length;) {
                        var d = a[c];
                        ++c;
                        b += d
                    }
                    return b
                },
                Y = function() {
                    return 2 + l + r + (D ? m : 0)
                },
                W = function() {
                    return a.width - 2 - x - Y()
                },
                A = function(b, d) {
                    null == w || w.startIndex != w.endIndex ? (w = new Mj(c.format, b, d), a.layoutGroups.push(w)) : (w.format = c.format, w.startIndex = b, w.endIndex = d)
                },
                sa = function() {
                    null != f.__ascent ? (k = f.size *
                        f.__ascent, q = f.size * f.__descent) : null != d && 0 != d.unitsPerEM ? (k = d.ascender / d.unitsPerEM * f.size, q = Math.abs(d.descender / d.unitsPerEM * f.size)) : (k = f.size, q = .185 * f.size);
                    h = f.leading;
                    z = Math.ceil(k + q + h);
                    z > C && (C = z);
                    k > g && (g = k)
                },
                Za = function() {
                    D = !0;
                    u = null != f.align ? f.align : 3;
                    r = null != f.blockIndent ? f.blockIndent : 0;
                    m = null != f.indent ? f.indent : 0;
                    l = null != f.leftMargin ? f.leftMargin : 0;
                    x = null != f.rightMargin ? f.rightMargin : 0
                },
                cb = function() {
                    if (b < a.textFormatRanges.get_length() - 1) {
                        b += 1;
                        c = a.textFormatRanges.get(b);
                        f.__merge(c.format);
                        var h = Nb.getFont(f);
                        Nb.__context.font = h;
                        d = Nb.getFontInstance(f);
                        return !0
                    }
                    return !1
                },
                B = function(d, f) {
                    if (d >= f) J = [], y = 0;
                    else if (f <= c.end) J = lb(a.text, d, f), y = Va(J);
                    else {
                        var h = d;
                        d = c.end;
                        var k = 0;
                        J = [];
                        for (y = 0;;)
                            if (h != d && (h = lb(a.text, h, d), J = J.concat(h)), d != f) {
                                if (!cb()) {
                                    Ga.warn("You found a bug in OpenFL's text code! Please save a copy of your project and create an issue on GitHub so we can fix this.", {
                                        fileName: "openfl/text/_internal/TextEngine.hx",
                                        lineNumber: 1121,
                                        className: "openfl.text._internal.TextEngine",
                                        methodName: "getLayoutGroups"
                                    });
                                    break
                                }
                                h = d;
                                d = f < c.end ? f : c.end;
                                ++k
                            } else {
                                y = Va(J);
                                break
                            } b -= k + 1;
                        cb()
                    }
                },
                L = function(b) {
                    if (b <= c.end) {
                        J = lb(a.text, H, b);
                        y = Va(J);
                        A(H, b);
                        w.positions = J;
                        var d = K,
                            f = Y();
                        w.offsetX = d + f;
                        w.ascent = k;
                        w.descent = q;
                        w.leading = h;
                        w.lineIndex = O;
                        w.offsetY = eb + 2;
                        w.width = y;
                        w.height = z;
                        K += y;
                        b == c.end && (w = null, cb(), sa())
                    } else
                        for (;;) {
                            var g = b < c.end ? b : c.end;
                            H != g && (J = lb(a.text, H, g), y = Va(J), A(H, g), w.positions = J, d = K, f = Y(), w.offsetX = d + f, w.ascent = k, w.descent = q, w.leading = h, w.lineIndex = O, w.offsetY = eb + 2, w.width = y,
                                w.height = z, K += y, H = g);
                            g == c.end && (w = null);
                            if (g == b) break;
                            if (!cb()) {
                                Ga.warn("You found a bug in OpenFL's text code! Please save a copy of your project and create an issue on GitHub so we can fix this.", {
                                    fileName: "openfl/text/_internal/TextEngine.hx",
                                    lineNumber: 1209,
                                    className: "openfl.text._internal.TextEngine",
                                    methodName: "getLayoutGroups"
                                });
                                break
                            }
                            sa()
                        }
                    H = b
                },
                aa = function() {
                    sa();
                    for (var b = a.layoutGroups.get_length(); - 1 < --b;) {
                        var c = a.layoutGroups.get(b);
                        if (c.lineIndex < O) break;
                        c.lineIndex > O || (c.ascent = g, c.height =
                            C)
                    }
                    eb += C;
                    C = g = 0;
                    O += 1;
                    K = 0;
                    D = !1
                },
                U = function(b) {
                    if (4 <= a.width && a.wordWrap) {
                        var c = J;
                        var d = Va(c);
                        for (var f = c.length - 1; 0 <= f;) {
                            var h = N.cca(a.text, H + f);
                            if (32 != h && 9 != h) break;
                            d -= c[f];
                            --f
                        }
                        for (; 0 < c.length && K + d > W();) {
                            for (h = f = d = 0; K + h < W();) {
                                var k = c[f];
                                0 == k ? (++f, ++d) : (h += k, ++f)
                            }
                            if (f == d) f = d + 1;
                            else
                                for (; 1 < f && K + h > W();) --f, 0 < f - d ? (B(H, H + f - d), h = y) : (f = 1, d = 0, B(H, H + 1), h = 0);
                            c = H + f - d;
                            L(c);
                            aa();
                            B(c, b);
                            c = J;
                            d = y
                        }
                    }
                    L(b)
                };
            cb();
            Za();
            sa();
            for (var $a, R = this.text.length + 1; H < R;)
                if (-1 < I && (-1 == G || I < G)) H <= I ? (B(H, I), U(I), w = null) : null != w && w.startIndex !=
                    w.endIndex && (w.endIndex == G && (w.width -= w.positions[w.positions.length - 1]), w = null), aa(), c.end == I && (cb(), sa()), H = I + 1, v = I, ++F, I = F < this.lineBreaks.get_length() ? this.lineBreaks.get(F) : -1, Za();
                else if (-1 < G)
                for (null != w && w.startIndex != w.endIndex && (w = null), $a = !1; H < this.text.length;) {
                    var S = -1; - 1 == G ? S = I : (S = G + 1, -1 < I && I < S && (S = I)); - 1 == S && (S = this.text.length);
                    B(H, S);
                    if (2 == u) {
                        if (0 < J.length && H == t) {
                            H += 1;
                            var Q = J.shift();
                            y -= Q;
                            K += Q
                        }
                        0 < J.length && S == G + 1 && (--S, Q = J.pop(), y -= Q)
                    }
                    this.wordWrap && K + y > W() && ($a = !0, 0 < J.length && S ==
                        G + 1 && K + y - J[J.length - 1] <= W() && ($a = !1));
                    if ($a) {
                        2 != u && (null != w || 0 < this.layoutGroups.get_length()) && ($a = w, null == $a && ($a = this.layoutGroups.get(this.layoutGroups.get_length() - 1)), $a.width -= $a.positions[$a.positions.length - 1], $a.endIndex--);
                        $a = this.layoutGroups.get_length() - 1;
                        for (Q = 0; 0 <= $a;) {
                            w = this.layoutGroups.get($a);
                            if (0 < $a && w.startIndex > t) ++Q;
                            else break;
                            --$a
                        }
                        H == t + 1 && aa();
                        K = 0;
                        if (0 < Q) {
                            $a = this.layoutGroups.get(this.layoutGroups.get_length() - Q).offsetX;
                            Q = this.layoutGroups.get_length() - Q;
                            for (var V = this.layoutGroups.get_length(); Q <
                                V;) {
                                var Pa = Q++;
                                w = this.layoutGroups.get(Pa);
                                w.offsetX -= $a;
                                w.offsetY = eb + 2;
                                w.lineIndex = O;
                                K += w.width
                            }
                        }
                        U(S);
                        $a = !1
                    } else null != w && H == G && t != G - 1 ? (2 != u && (w.endIndex = G, w.positions = w.positions.concat(J), w.width += y), K += y, H = S) : null == w || 2 == u ? (U(S), S == this.text.length && aa()) : (Q = S < c.end ? S : c.end, Q < S && (J = lb(this.text, H, Q), y = Va(J)), w.endIndex = Q, w.positions = w.positions.concat(J), w.width += y, K += y, Q == c.end && (w = null, cb(), sa(), H = Q, Q != S && L(S)), I == S && ++S, H = S, S == this.text.length && (aa(), -1 != I && (v = I, ++F, I = F < this.lineBreaks.get_length() ?
                        this.lineBreaks.get(F) : -1)));
                    S = this.text.indexOf(" ", H);
                    I == t && (w.endIndex = I, 0 > I - w.startIndex - w.positions.length && w.positions.push(0), H = I + 1);
                    t = G;
                    G = S;
                    if (-1 < I && I <= H && (G > I || -1 == G) || H > this.text.length) break
                } else H < this.text.length && (B(H, this.text.length), U(this.text.length), aa()), H += 1;
            v == H - 2 && -1 < v && (A(H - 1, H - 1), w.positions = [], w.ascent = k, w.descent = q, w.leading = h, w.lineIndex = O, w.offsetX = Y(), w.offsetY = eb + 2, w.width = 0, w.height = z)
        }
    },
    measureText: function(a) {
        return Nb.__context.measureText(a).width
    },
    restrictText: function(a) {
        if (null ==
            a) return a;
        null != this.__restrictRegexp && (a = this.__restrictRegexp.split(a).join(""));
        return a
    },
    setTextAlignment: function() {
        for (var a = -1, b = 0, c, d, f = !1, h = 0, k = this.layoutGroups.get_length(); h < k;) {
            var g = h++;
            c = this.layoutGroups.get(g);
            if (c.lineIndex != a) switch (a = c.lineIndex, b = this.width - 4 - c.format.rightMargin, c.format.align) {
                case 0:
                    b = this.lineWidths.get(a) < b ? Math.round((b - this.lineWidths.get(a)) / 2) : 0;
                    break;
                case 2:
                    if (this.lineWidths.get(a) < b) {
                        d = 1;
                        for (var q = g + 1, u = this.layoutGroups.get_length(); q < u;) {
                            var r =
                                q++;
                            if (this.layoutGroups.get(r).lineIndex == a) 0 != r && 32 != N.cca(this.text, this.layoutGroups.get(r).startIndex - 1) || ++d;
                            else break
                        }
                        if (1 < d && (c = this.layoutGroups.get(g + d - 1), q = N.cca(this.text, c.endIndex), c.endIndex < this.text.length && 10 != q && 13 != q)) {
                            b = (b - this.lineWidths.get(a)) / (d - 1);
                            f = !0;
                            q = 1;
                            do this.layoutGroups.get(g + q).offsetX += b * q; while (++q < d)
                        }
                    }
                    b = 0;
                    break;
                case 4:
                    b = this.lineWidths.get(a) < b ? Math.round(b - this.lineWidths.get(a)) : 0;
                    break;
                default:
                    b = 0
            }
            0 < b && (c.offsetX += b)
        }
        f && this.getLineMeasurements()
    },
    update: function() {
        null ==
            this.text || 0 == this.textFormatRanges.get_length() ? (this.lineAscents.set_length(0), this.lineBreaks.set_length(0), this.lineDescents.set_length(0), this.lineLeadings.set_length(0), this.lineHeights.set_length(0), this.lineWidths.set_length(0), this.layoutGroups.set_length(0), this.textHeight = this.textWidth = 0, this.numLines = 1, this.maxScrollH = 0, this.bottomScrollV = this.maxScrollV = 1) : (this.getLineBreaks(), this.getLayoutGroups(), this.getLineMeasurements(), this.setTextAlignment());
        this.getBounds()
    },
    get_bottomScrollV: function() {
        if (1 ==
            this.numLines || null == this.lineHeights) return 1;
        for (var a = this.lineHeights.get_length(), b = this.lineLeadings.get_length() == a ? -this.lineLeadings.get(a - 1) : 0, c = (0 < this.get_scrollV() ? this.get_scrollV() : 1) - 1, d = this.lineHeights.get_length(); c < d;) {
            var f = c++,
                h = this.lineHeights.get(f);
            b += h;
            if (b > this.height - 4) {
                a = f + (0 <= b - this.height ? 0 : 1);
                break
            }
        }
        return a < this.get_scrollV() ? this.get_scrollV() : a
    },
    get_maxScrollV: function() {
        if (1 == this.numLines || null == this.lineHeights) return 1;
        for (var a = this.numLines - 1, b = 0; 0 <= a;) {
            b +=
                this.lineHeights.get(a);
            if (b > this.height - 4) {
                a += 0 > b - this.height ? 1 : 2;
                break
            }--a
        }
        return 1 > a ? 1 : a
    },
    set_restrict: function(a) {
        if (this.restrict == a) return this.restrict;
        this.restrict = a;
        this.__restrictRegexp = null == this.restrict || 0 == this.restrict.length ? null : this.createRestrictRegexp(a);
        return this.restrict
    },
    get_scrollV: function() {
        if (1 == this.numLines || null == this.lineHeights) return 1;
        var a = this.get_maxScrollV();
        return this.scrollV > a ? a : this.scrollV
    },
    set_scrollV: function(a) {
        1 > a ? a = 1 : a > this.get_maxScrollV() && (a = this.get_maxScrollV());
        return this.scrollV = a
    },
    set_text: function(a) {
        return this.text = a
    },
    __class__: Nb,
    __properties__: {
        set_text: "set_text",
        set_scrollV: "set_scrollV",
        get_scrollV: "get_scrollV",
        set_restrict: "set_restrict",
        get_maxScrollV: "get_maxScrollV",
        get_bottomScrollV: "get_bottomScrollV"
    }
};
var od = function(a, b, c) {
    this.format = a;
    this.start = b;
    this.end = c
};
g["openfl.text._internal.TextFormatRange"] = od;
od.__name__ = "openfl.text._internal.TextFormatRange";
od.prototype = {
    __class__: od
};
var Mj = function(a, b, c) {
    this.format = a;
    this.startIndex =
        b;
    this.endIndex = c
};
g["openfl.text._internal.TextLayoutGroup"] = Mj;
Mj.__name__ = "openfl.text._internal.TextLayoutGroup";
Mj.prototype = {
    __class__: Mj
};
var qb = function() {
    oa.call(this);
    qb.__instances.push(this)
};
g["openfl.ui.GameInput"] = qb;
qb.__name__ = "openfl.ui.GameInput";
qb.__getDevice = function(a) {
    if (null == a) return null;
    if (null == qb.__devices.h.__keys__[a.__id__]) {
        var b = nc.__getDeviceData(),
            c = b[a.id].id;
        b = nc.__getDeviceData();
        b = new Vk(c, b[a.id].id);
        qb.__deviceList.push(b);
        qb.__devices.set(a, b);
        qb.numDevices =
            qb.__deviceList.length
    }
    return qb.__devices.h[a.__id__]
};
qb.__onGamepadAxisMove = function(a, b, c) {
    a = qb.__getDevice(a);
    if (null != a && a.enabled) {
        if (!a.__axis.h.hasOwnProperty(b)) {
            if (null == b) var d = "null";
            else switch (b) {
                case 0:
                    d = "LEFT_X";
                    break;
                case 1:
                    d = "LEFT_Y";
                    break;
                case 2:
                    d = "RIGHT_X";
                    break;
                case 3:
                    d = "RIGHT_Y";
                    break;
                case 4:
                    d = "TRIGGER_LEFT";
                    break;
                case 5:
                    d = "TRIGGER_RIGHT";
                    break;
                default:
                    d = "UNKNOWN (" + b + ")"
            }
            d = new Yd(a, "AXIS_" + d, -1, 1);
            a.__axis.h[b] = d;
            a.__controls.push(d)
        }
        d = a.__axis.h[b];
        d.value = c;
        d.dispatchEvent(new wa("change"))
    }
};
qb.__onGamepadButtonDown = function(a, b) {
    a = qb.__getDevice(a);
    if (null != a && a.enabled) {
        if (!a.__button.h.hasOwnProperty(b)) {
            if (null == b) var c = "null";
            else switch (b) {
                case 0:
                    c = "A";
                    break;
                case 1:
                    c = "B";
                    break;
                case 2:
                    c = "X";
                    break;
                case 3:
                    c = "Y";
                    break;
                case 4:
                    c = "BACK";
                    break;
                case 5:
                    c = "GUIDE";
                    break;
                case 6:
                    c = "START";
                    break;
                case 7:
                    c = "LEFT_STICK";
                    break;
                case 8:
                    c = "RIGHT_STICK";
                    break;
                case 9:
                    c = "LEFT_SHOULDER";
                    break;
                case 10:
                    c = "RIGHT_SHOULDER";
                    break;
                case 11:
                    c = "DPAD_UP";
                    break;
                case 12:
                    c = "DPAD_DOWN";
                    break;
                case 13:
                    c = "DPAD_LEFT";
                    break;
                case 14:
                    c = "DPAD_RIGHT";
                    break;
                default:
                    c = "UNKNOWN (" + b + ")"
            }
            c = new Yd(a, "BUTTON_" + c, 0, 1);
            a.__button.h[b] = c;
            a.__controls.push(c)
        }
        c = a.__button.h[b];
        c.value = 1;
        c.dispatchEvent(new wa("change"))
    }
};
qb.__onGamepadButtonUp = function(a, b) {
    a = qb.__getDevice(a);
    if (null != a && a.enabled) {
        if (!a.__button.h.hasOwnProperty(b)) {
            if (null == b) var c = "null";
            else switch (b) {
                case 0:
                    c = "A";
                    break;
                case 1:
                    c = "B";
                    break;
                case 2:
                    c = "X";
                    break;
                case 3:
                    c = "Y";
                    break;
                case 4:
                    c = "BACK";
                    break;
                case 5:
                    c = "GUIDE";
                    break;
                case 6:
                    c = "START";
                    break;
                case 7:
                    c = "LEFT_STICK";
                    break;
                case 8:
                    c = "RIGHT_STICK";
                    break;
                case 9:
                    c = "LEFT_SHOULDER";
                    break;
                case 10:
                    c = "RIGHT_SHOULDER";
                    break;
                case 11:
                    c = "DPAD_UP";
                    break;
                case 12:
                    c = "DPAD_DOWN";
                    break;
                case 13:
                    c = "DPAD_LEFT";
                    break;
                case 14:
                    c = "DPAD_RIGHT";
                    break;
                default:
                    c = "UNKNOWN (" + b + ")"
            }
            c = new Yd(a, "BUTTON_" + c, 0, 1);
            a.__button.h[b] = c;
            a.__controls.push(c)
        }
        c = a.__button.h[b];
        c.value = 0;
        c.dispatchEvent(new wa("change"))
    }
};
qb.__onGamepadConnect = function(a) {
    a = qb.__getDevice(a);
    if (null != a)
        for (var b = 0, c = qb.__instances; b < c.length;) {
            var d = c[b];
            ++b;
            d.dispatchEvent(new Qg("deviceAdded",
                !0, !1, a))
        }
};
qb.__onGamepadDisconnect = function(a) {
    var b = qb.__devices.h[a.__id__];
    if (null != b) {
        null != qb.__devices.h.__keys__[a.__id__] && (N.remove(qb.__deviceList, qb.__devices.h[a.__id__]), qb.__devices.remove(a));
        qb.numDevices = qb.__deviceList.length;
        a = 0;
        for (var c = qb.__instances; a < c.length;) {
            var d = c[a];
            ++a;
            d.dispatchEvent(new Qg("deviceRemoved", !0, !1, b))
        }
    }
};
qb.__super__ = oa;
qb.prototype = v(oa.prototype, {
    addEventListener: function(a, b, c, d, f) {
        null == f && (f = !1);
        null == d && (d = 0);
        null == c && (c = !1);
        oa.prototype.addEventListener.call(this,
            a, b, c, d, f);
        if ("deviceAdded" == a)
            for (a = 0, b = qb.__deviceList; a < b.length;) c = b[a], ++a, this.dispatchEvent(new Qg("deviceAdded", !0, !1, c))
    },
    __class__: qb
});
var Yd = function(a, b, c, d, f) {
    null == f && (f = 0);
    oa.call(this);
    this.device = a;
    this.id = b;
    this.minValue = c;
    this.maxValue = d;
    this.value = f
};
g["openfl.ui.GameInputControl"] = Yd;
Yd.__name__ = "openfl.ui.GameInputControl";
Yd.__super__ = oa;
Yd.prototype = v(oa.prototype, {
    __class__: Yd
});
var Vk = function(a, b) {
    this.__controls = [];
    this.__button = new mc;
    this.__axis = new mc;
    this.id = a;
    this.name =
        b;
    a = new Yd(this, "AXIS_0", -1, 1);
    this.__axis.h[0] = a;
    this.__controls.push(a);
    a = new Yd(this, "AXIS_1", -1, 1);
    this.__axis.h[1] = a;
    this.__controls.push(a);
    a = new Yd(this, "AXIS_2", -1, 1);
    this.__axis.h[2] = a;
    this.__controls.push(a);
    a = new Yd(this, "AXIS_3", -1, 1);
    this.__axis.h[3] = a;
    this.__controls.push(a);
    a = new Yd(this, "AXIS_4", -1, 1);
    this.__axis.h[4] = a;
    this.__controls.push(a);
    a = new Yd(this, "AXIS_5", -1, 1);
    this.__axis.h[5] = a;
    this.__controls.push(a);
    for (b = 0; 15 > b;) {
        var c = b++;
        a = new Yd(this, "BUTTON_" + c, 0, 1);
        this.__button.h[c] =
            a;
        this.__controls.push(a)
    }
};
g["openfl.ui.GameInputDevice"] = Vk;
Vk.__name__ = "openfl.ui.GameInputDevice";
Vk.prototype = {
    __class__: Vk
};
var ol = function() {};
g["openfl.ui.Keyboard"] = ol;
ol.__name__ = "openfl.ui.Keyboard";
ol.__getCharCode = function(a, b) {
    null == b && (b = !1);
    if (b) {
        switch (a) {
            case 48:
                return 41;
            case 49:
                return 33;
            case 50:
                return 64;
            case 51:
                return 35;
            case 52:
                return 36;
            case 53:
                return 37;
            case 54:
                return 94;
            case 55:
                return 38;
            case 56:
                return 42;
            case 57:
                return 40;
            case 186:
                return 58;
            case 187:
                return 43;
            case 188:
                return 60;
            case 189:
                return 95;
            case 190:
                return 62;
            case 191:
                return 63;
            case 192:
                return 126;
            case 219:
                return 123;
            case 220:
                return 124;
            case 221:
                return 125;
            case 222:
                return 34
        }
        if (65 <= a && 90 >= a) return a - 65 + 65
    } else {
        switch (a) {
            case 8:
                return 8;
            case 9:
                return 9;
            case 13:
                return 13;
            case 27:
                return 27;
            case 32:
                return 32;
            case 186:
                return 59;
            case 187:
                return 61;
            case 188:
                return 44;
            case 189:
                return 45;
            case 190:
                return 46;
            case 191:
                return 47;
            case 192:
                return 96;
            case 219:
                return 91;
            case 220:
                return 92;
            case 221:
                return 93;
            case 222:
                return 39
        }
        if (48 <=
            a && 57 >= a) return a - 48 + 48;
        if (65 <= a && 90 >= a) return a - 65 + 97
    }
    if (96 <= a && 105 >= a) return a - 96 + 48;
    switch (a) {
        case 8:
            return 8;
        case 13:
            return 13;
        case 46:
            return 127;
        case 106:
            return 42;
        case 107:
            return 43;
        case 108:
            return 44;
        case 110:
            return 45;
        case 111:
            return 46
    }
    return 0
};
var Ik = function() {};
g["openfl.ui.Mouse"] = Ik;
Ik.__name__ = "openfl.ui.Mouse";
var Nl = {
        toLimeCursor: function(a) {
            switch (a) {
                case "arrow":
                    return cc.ARROW;
                case "auto":
                    return cc.DEFAULT;
                case "button":
                    return cc.POINTER;
                case "crosshair":
                    return cc.CROSSHAIR;
                case "custom":
                    return cc.CUSTOM;
                case "hand":
                    return cc.MOVE;
                case "ibeam":
                    return cc.TEXT;
                case "resize_nesw":
                    return cc.RESIZE_NESW;
                case "resize_ns":
                    return cc.RESIZE_NS;
                case "resize_nwse":
                    return cc.RESIZE_NWSE;
                case "resize_we":
                    return cc.RESIZE_WE;
                case "wait":
                    return cc.WAIT;
                case "waitarrow":
                    return cc.WAIT_ARROW;
                default:
                    return cc.DEFAULT
            }
        }
    },
    ma = function(a) {
        null == a && (a = !1);
        this.debugEnabled = a;
        ma.initialized || ma.init()
    };
g["openfl.utils.AGALMiniAssembler"] = ma;
ma.__name__ = "openfl.utils.AGALMiniAssembler";
ma.init = function() {
    ma.initialized = !0;
    var a = ma.OPMAP,
        b = new Ya("mov", 2, 0, 0);
    a.h.mov = b;
    a = ma.OPMAP;
    b = new Ya("add", 3, 1, 0);
    a.h.add = b;
    a = ma.OPMAP;
    b = new Ya("sub", 3, 2, 0);
    a.h.sub = b;
    a = ma.OPMAP;
    b = new Ya("mul", 3, 3, 0);
    a.h.mul = b;
    a = ma.OPMAP;
    b = new Ya("div", 3, 4, 0);
    a.h.div = b;
    a = ma.OPMAP;
    b = new Ya("rcp", 2, 5, 0);
    a.h.rcp = b;
    a = ma.OPMAP;
    b = new Ya("min", 3, 6, 0);
    a.h.min = b;
    a = ma.OPMAP;
    b = new Ya("max", 3, 7, 0);
    a.h.max = b;
    a = ma.OPMAP;
    b = new Ya("frc", 2, 8, 0);
    a.h.frc = b;
    a = ma.OPMAP;
    b = new Ya("sqt", 2, 9, 0);
    a.h.sqt = b;
    a = ma.OPMAP;
    b = new Ya("rsq", 2, 10, 0);
    a.h.rsq = b;
    a = ma.OPMAP;
    b = new Ya("pow",
        3, 11, 0);
    a.h.pow = b;
    a = ma.OPMAP;
    b = new Ya("log", 2, 12, 0);
    a.h.log = b;
    a = ma.OPMAP;
    b = new Ya("exp", 2, 13, 0);
    a.h.exp = b;
    a = ma.OPMAP;
    b = new Ya("nrm", 2, 14, 0);
    a.h.nrm = b;
    a = ma.OPMAP;
    b = new Ya("sin", 2, 15, 0);
    a.h.sin = b;
    a = ma.OPMAP;
    b = new Ya("cos", 2, 16, 0);
    a.h.cos = b;
    a = ma.OPMAP;
    b = new Ya("crs", 3, 17, 0);
    a.h.crs = b;
    a = ma.OPMAP;
    b = new Ya("dp3", 3, 18, 0);
    a.h.dp3 = b;
    a = ma.OPMAP;
    b = new Ya("dp4", 3, 19, 0);
    a.h.dp4 = b;
    a = ma.OPMAP;
    b = new Ya("abs", 2, 20, 0);
    a.h.abs = b;
    a = ma.OPMAP;
    b = new Ya("neg", 2, 21, 0);
    a.h.neg = b;
    a = ma.OPMAP;
    b = new Ya("sat", 2, 22, 0);
    a.h.sat = b;
    a = ma.OPMAP;
    b = new Ya("m33", 3, 23, 16);
    a.h.m33 = b;
    a = ma.OPMAP;
    b = new Ya("m44", 3, 24, 16);
    a.h.m44 = b;
    a = ma.OPMAP;
    b = new Ya("m34", 3, 25, 16);
    a.h.m34 = b;
    a = ma.OPMAP;
    b = new Ya("ddx", 2, 26, 288);
    a.h.ddx = b;
    a = ma.OPMAP;
    b = new Ya("ddy", 2, 27, 288);
    a.h.ddy = b;
    a = ma.OPMAP;
    b = new Ya("ife", 2, 28, 897);
    a.h.ife = b;
    a = ma.OPMAP;
    b = new Ya("ine", 2, 29, 897);
    a.h.ine = b;
    a = ma.OPMAP;
    b = new Ya("ifg", 2, 30, 897);
    a.h.ifg = b;
    a = ma.OPMAP;
    b = new Ya("ifl", 2, 31, 897);
    a.h.ifl = b;
    a = ma.OPMAP;
    b = new Ya("els", 0, 32, 1921);
    a.h.els = b;
    a = ma.OPMAP;
    b = new Ya("eif", 0, 33, 1409);
    a.h.eif =
        b;
    a = ma.OPMAP;
    b = new Ya("kil", 1, 39, 160);
    a.h.kil = b;
    a = ma.OPMAP;
    b = new Ya("tex", 3, 40, 40);
    a.h.tex = b;
    a = ma.OPMAP;
    b = new Ya("sge", 3, 41, 0);
    a.h.sge = b;
    a = ma.OPMAP;
    b = new Ya("slt", 3, 42, 0);
    a.h.slt = b;
    a = ma.OPMAP;
    b = new Ya("sgn", 2, 43, 0);
    a.h.sgn = b;
    a = ma.OPMAP;
    b = new Ya("seq", 3, 44, 0);
    a.h.seq = b;
    a = ma.OPMAP;
    b = new Ya("sne", 3, 45, 0);
    a.h.sne = b;
    a = ma.SAMPLEMAP;
    b = new $b("rgba", 8, 0);
    a.h.rgba = b;
    a = ma.SAMPLEMAP;
    b = new $b("compressed", 8, 1);
    a.h.compressed = b;
    a = ma.SAMPLEMAP;
    b = new $b("compressedalpha", 8, 2);
    a.h.compressedalpha = b;
    a = ma.SAMPLEMAP;
    b = new $b("dxt1", 8, 1);
    a.h.dxt1 = b;
    a = ma.SAMPLEMAP;
    b = new $b("dxt5", 8, 2);
    a.h.dxt5 = b;
    a = ma.SAMPLEMAP;
    b = new $b("video", 8, 3);
    a.h.video = b;
    a = ma.SAMPLEMAP;
    b = new $b("2d", 12, 0);
    a.h["2d"] = b;
    a = ma.SAMPLEMAP;
    b = new $b("3d", 12, 2);
    a.h["3d"] = b;
    a = ma.SAMPLEMAP;
    b = new $b("cube", 12, 1);
    a.h.cube = b;
    a = ma.SAMPLEMAP;
    b = new $b("mipnearest", 24, 1);
    a.h.mipnearest = b;
    a = ma.SAMPLEMAP;
    b = new $b("miplinear", 24, 2);
    a.h.miplinear = b;
    a = ma.SAMPLEMAP;
    b = new $b("mipnone", 24, 0);
    a.h.mipnone = b;
    a = ma.SAMPLEMAP;
    b = new $b("nomip", 24, 0);
    a.h.nomip = b;
    a = ma.SAMPLEMAP;
    b = new $b("nearest", 28, 0);
    a.h.nearest = b;
    a = ma.SAMPLEMAP;
    b = new $b("linear", 28, 1);
    a.h.linear = b;
    a = ma.SAMPLEMAP;
    b = new $b("anisotropic2x", 28, 2);
    a.h.anisotropic2x = b;
    a = ma.SAMPLEMAP;
    b = new $b("anisotropic4x", 28, 3);
    a.h.anisotropic4x = b;
    a = ma.SAMPLEMAP;
    b = new $b("anisotropic8x", 28, 4);
    a.h.anisotropic8x = b;
    a = ma.SAMPLEMAP;
    b = new $b("anisotropic16x", 28, 5);
    a.h.anisotropic16x = b;
    a = ma.SAMPLEMAP;
    b = new $b("centroid", 16, 1);
    a.h.centroid = b;
    a = ma.SAMPLEMAP;
    b = new $b("single", 16, 2);
    a.h.single = b;
    a = ma.SAMPLEMAP;
    b = new $b("ignoresampler",
        16, 4);
    a.h.ignoresampler = b;
    a = ma.SAMPLEMAP;
    b = new $b("repeat", 20, 1);
    a.h.repeat = b;
    a = ma.SAMPLEMAP;
    b = new $b("wrap", 20, 1);
    a.h.wrap = b;
    a = ma.SAMPLEMAP;
    b = new $b("clamp", 20, 0);
    a.h.clamp = b;
    a = ma.SAMPLEMAP;
    b = new $b("clamp_u_repeat_v", 20, 2);
    a.h.clamp_u_repeat_v = b;
    a = ma.SAMPLEMAP;
    b = new $b("repeat_u_clamp_v", 20, 3);
    a.h.repeat_u_clamp_v = b
};
ma.prototype = {
    assemble: function(a, b, c, d) {
        null == d && (d = !1);
        null == c && (c = 1);
        var f = Ra.getTimer();
        this.agalcode = new Vc(0);
        this.error = "";
        var h = !1;
        "fragment" == a ? h = !0 : "vertex" != a && (this.error =
            'ERROR: mode needs to be "fragment" or "vertex" but is "' + a + '".');
        this.agalcode.__endian = 1;
        this.agalcode.writeByte(160);
        this.agalcode.writeUnsignedInt(c);
        this.agalcode.writeByte(161);
        this.agalcode.writeByte(h ? 1 : 0);
        this.initregmap(c, d);
        a = O.replace(b, "\r", "\n").split("\n");
        b = 0;
        d = a.length;
        for (var k = new ja("<.*>", "g"), g = new ja("([\\w\\.\\-\\+]+)", "gi"), q = new ja("^\\w{3}", "ig"), u = new ja("vc\\[([vofi][acostdip]?[d]?)(\\d*)?(\\.[xyzw](\\+\\d{1,3})?)?\\](\\.[xyzw]{1,4})?|([vofi][acostdip]?[d]?)(\\d*)?(\\.[xyzw]{1,4})?",
                "gi"), r = new ja("\\[.*\\]", "ig"), m = new ja("^\\b[A-Za-z]{1,3}", "ig"), l = new ja("\\d+", ""), x = new ja("(\\.[xyzw]{1,4})", ""), w = new ja("[A-Za-z]{1,3}", "ig"), D = new ja("(\\.[xyzw]{1,1})", ""), J = new ja("\\+\\d{1,3}", "ig"), y = 0; y < d && "" == this.error;) {
            var z = O.trim(a[y]),
                C = z.indexOf("//"); - 1 != C && (z = N.substr(z, 0, C));
            var t = k.match(z) ? k.matchedPos().pos : -1;
            C = null; - 1 != t && (C = this.match(N.substr(z, t, null), g), z = N.substr(z, 0, t));
            var v = null;
            q.match(z) && (t = q.matched(0), v = ma.OPMAP.h[t]);
            if (null == v) 3 <= z.length && Ga.warn("warning: bad line " +
                y + ": " + a[y], {
                    fileName: "openfl/utils/AGALMiniAssembler.hx",
                    lineNumber: 262,
                    className: "openfl.utils.AGALMiniAssembler",
                    methodName: "assemble"
                });
            else if (this.debugEnabled && Ga.info(v, {
                    fileName: "openfl/utils/AGALMiniAssembler.hx",
                    lineNumber: 272,
                    className: "openfl.utils.AGALMiniAssembler",
                    methodName: "assemble"
                }), null == v) 3 <= z.length && Ga.warn("warning: bad line " + y + ": " + a[y], {
                fileName: "openfl/utils/AGALMiniAssembler.hx",
                lineNumber: 279,
                className: "openfl.utils.AGALMiniAssembler",
                methodName: "assemble"
            });
            else {
                z =
                    N.substr(z, z.indexOf(v.name) + v.name.length, null);
                if (0 != (v.flags & 256) && 2 > c) {
                    this.error = "error: opcode requires version 2.";
                    break
                }
                if (0 != (v.flags & 64) && h) {
                    this.error = "error: opcode is only allowed in vertex programs.";
                    break
                }
                if (0 != (v.flags & 32) && !h) {
                    this.error = "error: opcode is only allowed in fragment programs.";
                    break
                }
                this.verbose && Ga.info("emit opcode=" + H.string(v), {
                    fileName: "openfl/utils/AGALMiniAssembler.hx",
                    lineNumber: 308,
                    className: "openfl.utils.AGALMiniAssembler",
                    methodName: "assemble"
                });
                this.agalcode.writeUnsignedInt(v.emitCode);
                ++b;
                if (4096 < b) {
                    this.error = "error: too many opcodes. maximum is 4096.";
                    break
                }
                var G = this.match(z, u);
                if (G.length != v.numRegister) {
                    this.error = "error: wrong number of operands. found " + G.length + " but expected " + v.numRegister + ".";
                    break
                }
                var I = !1,
                    F = 160;
                z = 0;
                for (t = G.length; z < t;) {
                    var K = z++,
                        eb = !1,
                        Va = this.match(G[K], r);
                    0 < Va.length && (G[K] = O.replace(G[K], Va[0], "0"), this.verbose && Ga.info("IS REL", {
                            fileName: "openfl/utils/AGALMiniAssembler.hx",
                            lineNumber: 344,
                            className: "openfl.utils.AGALMiniAssembler",
                            methodName: "assemble"
                        }),
                        eb = !0);
                    var lb = this.match(G[K], m);
                    if (0 == lb.length) {
                        this.error = "error: could not parse operand " + K + " (" + G[K] + ").";
                        I = !0;
                        break
                    }
                    var Y = ma.REGMAP.h[lb[0]];
                    this.debugEnabled && Ga.info(Y, {
                        fileName: "openfl/utils/AGALMiniAssembler.hx",
                        lineNumber: 363,
                        className: "openfl.utils.AGALMiniAssembler",
                        methodName: "assemble"
                    });
                    if (null == Y) {
                        this.error = "error: could not find register name for operand " + K + " (" + G[K] + ").";
                        I = !0;
                        break
                    }
                    if (h) {
                        if (0 == (Y.flags & 32)) {
                            this.error = "error: register operand " + K + " (" + G[K] + ") only allowed in vertex programs.";
                            I = !0;
                            break
                        }
                        if (eb) {
                            this.error = "error: register operand " + K + " (" + G[K] + ") relative adressing not allowed in fragment programs.";
                            I = !0;
                            break
                        }
                    } else if (0 == (Y.flags & 64)) {
                        this.error = "error: register operand " + K + " (" + G[K] + ") only allowed in fragment programs.";
                        I = !0;
                        break
                    }
                    G[K] = N.substr(G[K], G[K].indexOf(Y.name) + Y.name.length, null);
                    var W = eb ? this.match(Va[0], l) : this.match(G[K], l);
                    lb = 0;
                    0 < W.length && (lb = H.parseInt(W[0]));
                    if (cb.gt(lb, Y.range)) {
                        z = Y.range + 1;
                        this.error = "error: register operand " + K + " (" + G[K] + ") index exceeds limit of " +
                            (null == z ? "null" : H.string(cb.toFloat(z))) + ".";
                        I = !0;
                        break
                    }
                    var sa = this.match(G[K], x),
                        E = 0 == K && 0 == (v.flags & 128),
                        A = 2 == K && 0 != (v.flags & 8),
                        Za = 0,
                        B = 0,
                        L = 0;
                    if (E && eb) {
                        this.error = "error: relative can not be destination";
                        I = !0;
                        break
                    }
                    if (0 < sa.length) {
                        for (var $a = W = 0, aa = sa[0].length, S = 1; S < aa;) $a = N.cca(sa[0], S) - 120, cb.gt($a, 2) && ($a = 3), W = E ? W | 1 << $a : W | $a << (S - 1 << 1), ++S;
                        if (!E)
                            for (; 4 >= S;) W |= $a << (S - 1 << 1), ++S
                    } else W = E ? 15 : 228;
                    if (eb) {
                        sa = this.match(Va[0], w);
                        Za = ma.REGMAP.h[sa[0]];
                        if (null == Za) {
                            this.error = "error: bad index register";
                            I = !0;
                            break
                        }
                        Za = Za.emitCode;
                        $a = this.match(Va[0], D);
                        if (0 == $a.length) {
                            this.error = "error: bad index register select";
                            I = !0;
                            break
                        }
                        B = N.cca($a[0], 1) - 120;
                        cb.gt(B, 2) && (B = 3);
                        Va = this.match(Va[0], J);
                        0 < Va.length && (L = H.parseInt(Va[0]));
                        if (0 > L || 255 < L) {
                            this.error = "error: index offset " + L + " out of bounds. [0..255]";
                            I = !0;
                            break
                        }
                        this.verbose && Ga.info("RELATIVE: type=" + Za + "==" + sa[0] + " sel=" + (null == B ? "null" : H.string(cb.toFloat(B))) + "==" + $a[0] + " idx=" + (null == lb ? "null" : H.string(cb.toFloat(lb))) + " offset=" + L, {
                            fileName: "openfl/utils/AGALMiniAssembler.hx",
                            lineNumber: 518,
                            className: "openfl.utils.AGALMiniAssembler",
                            methodName: "assemble"
                        })
                    }
                    this.verbose && Ga.info("  emit argcode=" + H.string(Y) + "[" + (null == lb ? "null" : H.string(cb.toFloat(lb))) + "][" + W + "]", {
                        fileName: "openfl/utils/AGALMiniAssembler.hx",
                        lineNumber: 525,
                        className: "openfl.utils.AGALMiniAssembler",
                        methodName: "assemble"
                    });
                    if (E) this.agalcode.writeShort(lb), this.agalcode.writeByte(W), this.agalcode.writeByte(Y.emitCode), F -= 32;
                    else {
                        if (A) {
                            this.verbose && Ga.info("  emit sampler", {
                                fileName: "openfl/utils/AGALMiniAssembler.hx",
                                lineNumber: 541,
                                className: "openfl.utils.AGALMiniAssembler",
                                methodName: "assemble"
                            });
                            K = 5;
                            Va = eb = 0;
                            for (Y = null == C ? 0 : C.length; Va < Y;) W = Va++, this.verbose && Ga.info("    opt: " + C[W], {
                                    fileName: "openfl/utils/AGALMiniAssembler.hx",
                                    lineNumber: 552,
                                    className: "openfl.utils.AGALMiniAssembler",
                                    methodName: "assemble"
                                }), E = ma.SAMPLEMAP.h[C[W]], null == E ? (eb = parseFloat(C[W]), this.verbose && Ga.info("    bias: " + eb, {
                                    fileName: "openfl/utils/AGALMiniAssembler.hx",
                                    lineNumber: 565,
                                    className: "openfl.utils.AGALMiniAssembler",
                                    methodName: "assemble"
                                })) :
                                (16 != E.flag && (K &= ~(15 << E.flag)), K |= E.mask << E.flag);
                            this.agalcode.writeShort(lb);
                            this.agalcode.writeByte(8 * eb | 0);
                            this.agalcode.writeByte(0);
                            this.agalcode.writeUnsignedInt(K);
                            this.verbose && Ga.info("    bits: " + (K - 5), {
                                fileName: "openfl/utils/AGALMiniAssembler.hx",
                                lineNumber: 586,
                                className: "openfl.utils.AGALMiniAssembler",
                                methodName: "assemble"
                            })
                        } else 0 == K && (this.agalcode.writeUnsignedInt(0), F -= 32), this.agalcode.writeShort(lb), this.agalcode.writeByte(L), this.agalcode.writeByte(W), this.agalcode.writeByte(Y.emitCode),
                            this.agalcode.writeByte(Za), this.agalcode.writeShort(eb ? B | 32768 : 0);
                        F -= 64
                    }
                }
                for (z = 0; z < F;) this.agalcode.writeByte(0), z += 8;
                if (I) break
            }++y
        }
        "" != this.error && (this.error += "\n  at line " + y + " " + a[y], Td.set_length(this.agalcode, 0), Ga.info(this.error, {
            fileName: "openfl/utils/AGALMiniAssembler.hx",
            lineNumber: 631,
            className: "openfl.utils.AGALMiniAssembler",
            methodName: "assemble"
        }));
        if (this.debugEnabled) {
            c = "generated bytecode:";
            h = Td.get_length(this.agalcode);
            z = 0;
            for (t = h; z < t;) h = z++, 0 == h % 16 && (c += "\n"), 0 == h % 4 && (c += " "),
                h = O.hex(this.agalcode.b[h], 2), 2 > h.length && (h = "0" + h), c += h;
            Ga.info(c, {
                fileName: "openfl/utils/AGALMiniAssembler.hx",
                lineNumber: 662,
                className: "openfl.utils.AGALMiniAssembler",
                methodName: "assemble"
            })
        }
        this.verbose && Ga.info("AGALMiniAssembler.assemble time: " + (Ra.getTimer() - f) / 1E3 + "s", {
            fileName: "openfl/utils/AGALMiniAssembler.hx",
            lineNumber: 667,
            className: "openfl.utils.AGALMiniAssembler",
            methodName: "assemble"
        });
        return this.agalcode
    },
    initregmap: function(a, b) {
        var c = ma.REGMAP,
            d = new Md("va", "vertex attribute",
                0, b ? 1024 : 1 == a || 2 == a ? 7 : 15, 66);
        c.h.va = d;
        c = ma.REGMAP;
        d = new Md("vc", "vertex constant", 1, b ? 1024 : 1 == a ? 127 : 249, 66);
        c.h.vc = d;
        c = ma.REGMAP;
        d = new Md("vt", "vertex temporary", 2, b ? 1024 : 1 == a ? 7 : 25, 67);
        c.h.vt = d;
        c = ma.REGMAP;
        d = new Md("vo", "vertex output", 3, b ? 1024 : 0, 65);
        c.h.vo = d;
        c = ma.REGMAP;
        d = new Md("vi", "varying", 4, b ? 1024 : 1 == a ? 7 : 9, 99);
        c.h.vi = d;
        c = ma.REGMAP;
        d = new Md("fc", "fragment constant", 1, b ? 1024 : 1 == a ? 27 : 2 == a ? 63 : 199, 34);
        c.h.fc = d;
        c = ma.REGMAP;
        d = new Md("ft", "fragment temporary", 2, b ? 1024 : 1 == a ? 7 : 25, 35);
        c.h.ft = d;
        c = ma.REGMAP;
        d = new Md("fs", "texture sampler", 5, b ? 1024 : 7, 34);
        c.h.fs = d;
        c = ma.REGMAP;
        d = new Md("fo", "fragment output", 3, b ? 1024 : 1 == a ? 0 : 3, 33);
        c.h.fo = d;
        c = ma.REGMAP;
        d = new Md("fd", "fragment depth output", 6, b ? 1024 : 1 == a ? -1 : 0, 33);
        c.h.fd = d;
        c = ma.REGMAP;
        d = new Md("iid", "instance id", 7, b ? 1024 : 0, 66);
        c.h.iid = d;
        d = ma.REGMAP.h.vo;
        ma.REGMAP.h.op = d;
        d = ma.REGMAP.h.vi;
        ma.REGMAP.h.i = d;
        d = ma.REGMAP.h.vi;
        ma.REGMAP.h.v = d;
        d = ma.REGMAP.h.fo;
        ma.REGMAP.h.oc = d;
        d = ma.REGMAP.h.fd;
        ma.REGMAP.h.od = d;
        d = ma.REGMAP.h.vi;
        ma.REGMAP.h.fi = d
    },
    match: function(a, b) {
        for (var c = [], d = 0; b.matchSub(a, d);) d = b.matched(0), c.push(d), d = b.matchedPos().pos + d.length;
        return c
    },
    __class__: ma
};
var Ya = function(a, b, c, d) {
    this.name = a;
    this.numRegister = b;
    this.emitCode = c;
    this.flags = d
};
g["openfl.utils._AGALMiniAssembler.OpCode"] = Ya;
Ya.__name__ = "openfl.utils._AGALMiniAssembler.OpCode";
Ya.prototype = {
    toString: function() {
        return '[OpCode name="' + this.name + '", numRegister=' + this.numRegister + ", emitCode=" + this.emitCode + ", flags=" + this.flags + "]"
    },
    __class__: Ya
};
var Md = function(a, b, c, d, f) {
    this.name = a;
    this.longName =
        b;
    this.emitCode = c;
    this.range = d;
    this.flags = f
};
g["openfl.utils._AGALMiniAssembler.Register"] = Md;
Md.__name__ = "openfl.utils._AGALMiniAssembler.Register";
Md.prototype = {
    toString: function() {
        return '[Register name="' + this.name + '", longName="' + this.longName + '", emitCode=' + (null == this.emitCode ? "null" : H.string(cb.toFloat(this.emitCode))) + ", range=" + (null == this.range ? "null" : H.string(cb.toFloat(this.range))) + ", flags=" + (null == this.flags ? "null" : H.string(cb.toFloat(this.flags))) + "]"
    },
    __class__: Md
};
var $b = function(a,
    b, c) {
    this.name = a;
    this.flag = b;
    this.mask = c
};
g["openfl.utils._AGALMiniAssembler.Sampler"] = $b;
$b.__name__ = "openfl.utils._AGALMiniAssembler.Sampler";
$b.prototype = {
    __class__: $b
};
var Nj = function() {};
g["openfl.utils.IAssetCache"] = Nj;
Nj.__name__ = "openfl.utils.IAssetCache";
Nj.__isInterface__ = !0;
Nj.prototype = {
    __class__: Nj,
    __properties__: {
        get_enabled: "get_enabled"
    }
};
var Oj = function() {
    this.__enabled = !0;
    this.bitmapData = new Qa;
    this.font = new Qa;
    this.sound = new Qa
};
g["openfl.utils.AssetCache"] = Oj;
Oj.__name__ = "openfl.utils.AssetCache";
Oj.__interfaces__ = [Nj];
Oj.prototype = {
    getFont: function(a) {
        return this.font.h[a]
    },
    hasFont: function(a) {
        return Object.prototype.hasOwnProperty.call(this.font.h, a)
    },
    setFont: function(a, b) {
        this.font.h[a] = b
    },
    get_enabled: function() {
        return this.__enabled
    },
    __class__: Oj,
    __properties__: {
        get_enabled: "get_enabled"
    }
};
var cf = function() {
    Wb.call(this)
};
g["openfl.utils.AssetLibrary"] = cf;
cf.__name__ = "openfl.utils.AssetLibrary";
cf.fromBundle = function(a) {
    a = Wb.fromBundle(a);
    if (null != a) {
        if (a instanceof cf) return a;
        var b =
            new cf;
        b.__proxy = a;
        return b
    }
    return null
};
cf.fromManifest = function(a) {
    a = Wb.fromManifest(a);
    if (null != a) {
        if (a instanceof cf) return a;
        var b = new cf;
        b.__proxy = a;
        return b
    }
    return null
};
cf.__super__ = Wb;
cf.prototype = v(Wb.prototype, {
    bind: function(a, b) {
        return !1
    },
    exists: function(a, b) {
        return null != this.__proxy ? this.__proxy.exists(a, b) : Wb.prototype.exists.call(this, a, b)
    },
    getAsset: function(a, b) {
        return null != this.__proxy ? this.__proxy.getAsset(a, b) : Wb.prototype.getAsset.call(this, a, b)
    },
    getAudioBuffer: function(a) {
        return null !=
            this.__proxy ? this.__proxy.getAudioBuffer(a) : Wb.prototype.getAudioBuffer.call(this, a)
    },
    getBytes: function(a) {
        return null != this.__proxy ? this.__proxy.getBytes(a) : Wb.prototype.getBytes.call(this, a)
    },
    getFont: function(a) {
        return null != this.__proxy ? this.__proxy.getFont(a) : Wb.prototype.getFont.call(this, a)
    },
    getImage: function(a) {
        return null != this.__proxy ? this.__proxy.getImage(a) : Wb.prototype.getImage.call(this, a)
    },
    getPath: function(a) {
        return null != this.__proxy ? this.__proxy.getPath(a) : Wb.prototype.getPath.call(this,
            a)
    },
    getText: function(a) {
        return null != this.__proxy ? this.__proxy.getText(a) : Wb.prototype.getText.call(this, a)
    },
    isLocal: function(a, b) {
        return null != this.__proxy ? this.__proxy.isLocal(a, b) : Wb.prototype.isLocal.call(this, a, b)
    },
    load: function() {
        return null != this.__proxy ? this.__proxy.load() : Wb.prototype.load.call(this)
    },
    loadAudioBuffer: function(a) {
        return null != this.__proxy ? this.__proxy.loadAudioBuffer(a) : Wb.prototype.loadAudioBuffer.call(this, a)
    },
    loadBytes: function(a) {
        return null != this.__proxy ? this.__proxy.loadBytes(a) :
            Wb.prototype.loadBytes.call(this, a)
    },
    loadFont: function(a) {
        return null != this.__proxy ? this.__proxy.loadFont(a) : Wb.prototype.loadFont.call(this, a)
    },
    loadImage: function(a) {
        return null != this.__proxy ? this.__proxy.loadImage(a) : Wb.prototype.loadImage.call(this, a)
    },
    loadText: function(a) {
        return null != this.__proxy ? this.__proxy.loadText(a) : Wb.prototype.loadText.call(this, a)
    },
    unload: function() {
        null != this.__proxy ? this.__proxy.unload() : Wb.prototype.unload.call(this)
    },
    __class__: cf
});
var ac = function() {};
g["openfl.utils.Assets"] =
    ac;
ac.__name__ = "openfl.utils.Assets";
ac.exists = function(a, b) {
    return Fa.exists(a, b)
};
ac.getFont = function(a, b) {
    null == b && (b = !0);
    if (b && ac.cache.get_enabled() && ac.cache.hasFont(a)) return ac.cache.getFont(a);
    var c = Fa.getFont(a, !1);
    if (null != c) {
        var d = new ha;
        d.__fromLimeFont(c);
        b && ac.cache.get_enabled() && ac.cache.setFont(a, d);
        return d
    }
    return new ha
};
ac.getText = function(a) {
    return Fa.getText(a)
};
var Td = {
        __properties__: {
            set_length: "set_length",
            get_length: "get_length"
        },
        fromArrayBuffer: function(a) {
            return null ==
                a ? null : Vc.fromBytes(zb.ofData(a))
        },
        fromBytes: function(a) {
            return null == a ? null : a instanceof Vc ? a : Vc.fromBytes(a)
        },
        toArrayBuffer: function(a) {
            return a.b.bufferValue
        },
        toBytes: function(a) {
            return a
        },
        get_length: function(a) {
            return null == a ? 0 : a.length
        },
        set_length: function(a, b) {
            0 <= b && (a.__resize(b), b < a.position && (a.position = b));
            return a.length = b
        }
    },
    rl = function() {};
g["openfl.utils.IDataOutput"] = rl;
rl.__name__ = "openfl.utils.IDataOutput";
rl.__isInterface__ = !0;
var sl = function() {};
g["openfl.utils.IDataInput"] = sl;
sl.__name__ =
    "openfl.utils.IDataInput";
sl.__isInterface__ = !0;
var Vc = function(a) {
    null == a && (a = 0);
    var b = new zb(new ArrayBuffer(a));
    zb.call(this, b.b.buffer);
    this.__allocated = a;
    null == Vc.__defaultEndian && (Cc.get_endianness() == Xi.LITTLE_ENDIAN ? Vc.__defaultEndian = 1 : Vc.__defaultEndian = 0);
    this.__endian = Vc.__defaultEndian;
    this.objectEncoding = Vc.defaultObjectEncoding;
    this.position = 0
};
g["openfl.utils.ByteArrayData"] = Vc;
Vc.__name__ = "openfl.utils.ByteArrayData";
Vc.__interfaces__ = [rl, sl];
Vc.fromBytes = function(a) {
    var b = new Vc;
    b.__fromBytes(a);
    return b
};
Vc.__super__ = zb;
Vc.prototype = v(zb.prototype, {
    readByte: function() {
        var a = this.readUnsignedByte();
        return 0 != (a & 128) ? a - 256 : a
    },
    readInt: function() {
        var a = this.readUnsignedByte(),
            b = this.readUnsignedByte(),
            c = this.readUnsignedByte(),
            d = this.readUnsignedByte();
        return 1 == this.__endian ? d << 24 | c << 16 | b << 8 | a : a << 24 | b << 16 | c << 8 | d
    },
    readUnsignedByte: function() {
        if (this.position < this.length) return this.b[this.position++];
        throw new Hh;
    },
    readUnsignedInt: function() {
        var a = this.readUnsignedByte(),
            b =
            this.readUnsignedByte(),
            c = this.readUnsignedByte(),
            d = this.readUnsignedByte();
        return 1 == this.__endian ? d << 24 | c << 16 | b << 8 | a : a << 24 | b << 16 | c << 8 | d
    },
    readUnsignedShort: function() {
        var a = this.readUnsignedByte(),
            b = this.readUnsignedByte();
        return 1 == this.__endian ? (b << 8) + a : a << 8 | b
    },
    readUTF: function() {
        var a = this.readUnsignedShort();
        return this.readUTFBytes(a)
    },
    readUTFBytes: function(a) {
        if (this.position + a > this.length) throw new Hh;
        this.position += a;
        return this.getString(this.position - a, a)
    },
    writeByte: function(a) {
        this.__resize(this.position +
            1);
        this.b[this.position++] = a & 255
    },
    writeBytes: function(a, b, c) {
        null == c && (c = 0);
        null == b && (b = 0);
        0 != Td.get_length(a) && (0 == c && (c = Td.get_length(a) - b), this.__resize(this.position + c), this.blit(this.position, a, b, c), this.position += c)
    },
    writeInt: function(a) {
        this.__resize(this.position + 4);
        1 == this.__endian ? (this.b[this.position++] = a & 255, this.b[this.position++] = a >> 8 & 255, this.b[this.position++] = a >> 16 & 255, this.b[this.position++] = a >> 24 & 255) : (this.b[this.position++] = a >> 24 & 255, this.b[this.position++] = a >> 16 & 255, this.b[this.position++] =
            a >> 8 & 255, this.b[this.position++] = a & 255)
    },
    writeShort: function(a) {
        this.__resize(this.position + 2);
        1 == this.__endian ? (this.b[this.position++] = a & 255, this.b[this.position++] = a >> 8 & 255) : (this.b[this.position++] = a >> 8 & 255, this.b[this.position++] = a & 255)
    },
    writeUnsignedInt: function(a) {
        this.writeInt(a)
    },
    __fromBytes: function(a) {
        this.b = a.b;
        this.__allocated = a.length;
        this.data = a.data;
        this.length = a.length
    },
    __resize: function(a) {
        if (a > this.__allocated) {
            var b = new zb(new ArrayBuffer(3 * (a + 1) >> 1));
            if (0 < this.__allocated) {
                var c =
                    this.length;
                this.length = this.__allocated;
                b.blit(0, this, 0, this.__allocated);
                this.length = c
            }
            this.b = b.b;
            this.__allocated = b.length;
            this.data = b.data
        }
        this.length < a && (this.length = a)
    },
    __class__: Vc
});
var Sd = {
        __get: function(a, b) {
            if (null == a || null == b) return null;
            if (Object.prototype.hasOwnProperty.call(a, b)) return ya.field(a, b);
            if (a instanceof kb) {
                var c = a.getChildByName(b);
                if (null != c) return c
            }
            return ya.getProperty(a, b)
        },
        __eq: function(a, b) {
            return a == b
        },
        __neq: function(a, b) {
            return a != b
        }
    },
    Pj = function() {};
g["haxe.lang.Iterator"] =
    Pj;
Pj.__name__ = "haxe.lang.Iterator";
Pj.__isInterface__ = !0;
Pj.prototype = {
    __class__: Pj
};
var Qj = function() {};
g["haxe.lang.Iterable"] = Qj;
Qj.__name__ = "haxe.lang.Iterable";
Qj.__isInterface__ = !0;
Qj.prototype = {
    __class__: Qj
};
var Ii = function(a, b) {
    null == b && (b = 0);
    if (isNaN(a) || 0 > a) throw new Vb("The delay specified is negative or not a finite number");
    oa.call(this);
    this.__delay = a;
    this.__repeatCount = b;
    this.running = !1;
    this.currentCount = 0
};
g["openfl.utils.Timer"] = Ii;
Ii.__name__ = "openfl.utils.Timer";
Ii.__super__ = oa;
Ii.prototype = v(oa.prototype, {
    start: function() {
        this.running || (this.running = !0, this.__timerID = window.setInterval(l(this, this.timer_onTimer), this.__delay | 0))
    },
    stop: function() {
        this.running = !1;
        null != this.__timerID && (window.clearInterval(this.__timerID), this.__timerID = null)
    },
    __handleUpdateAfterEvent: function() {
        null != Ra.get_current() && null != Ra.get_current().stage && Ra.get_current().stage.__renderAfterEvent()
    },
    timer_onTimer: function() {
        this.currentCount++;
        if (0 < this.__repeatCount && this.currentCount >= this.__repeatCount) {
            this.stop();
            var a = new Sg("timer");
            this.dispatchEvent(a);
            a.__updateAfterEventFlag && this.__handleUpdateAfterEvent();
            a = new Sg("timerComplete")
        } else a = new Sg("timer");
        this.dispatchEvent(a);
        a.__updateAfterEventFlag && this.__handleUpdateAfterEvent()
    },
    __class__: Ii
});
var Dc = function() {};
g["openfl.utils._internal.Lib"] = Dc;
Dc.__name__ = "openfl.utils._internal.Lib";
var ag = function() {
    this.rollOutStack = []
};
g["openfl.utils._internal.TouchData"] = ag;
ag.__name__ = "openfl.utils._internal.TouchData";
ag.prototype = {
    reset: function() {
        this.touchOverTarget =
            this.touchDownTarget = this.touch = null;
        this.rollOutStack.splice(0, this.rollOutStack.length)
    },
    __class__: ag
};
E.$haxeUID |= 0;
"undefined" != typeof performance && "function" == typeof performance.now && (N.now = performance.now.bind(performance));
g.Math = Math;
null == String.fromCodePoint && (String.fromCodePoint = function(a) {
    return 65536 > a ? String.fromCharCode(a) : String.fromCharCode((a >> 10) + 55232) + String.fromCharCode((a & 1023) + 56320)
});
Object.defineProperty(String.prototype, "__class__", {
    value: g.String = String,
    enumerable: !1,
    writable: !0
});
String.__name__ = "String";
g.Array = Array;
Array.__name__ = "Array";
Date.prototype.__class__ = g.Date = Date;
Date.__name__ = "Date";
var vl = {},
    Hl = {},
    Il = Number,
    Gl = Boolean,
    xl = {},
    yl = {};
zd.NIL = new zd(null, null);
va.__toStr = {}.toString;
"undefined" == typeof window && (E.onmessage = function(a) {
    a = a.data;
    try {
        E.onmessage = (G = Ad.__current, l(G, G.dispatchMessage)), Fl.toFunction(a)()
    } catch (b) {
        Ta.lastError = b, Ad.__current.destroy()
    }
});
R.__alpha16 = new Uint32Array(256);
for (var Vg = 0; 256 > Vg;) {
    var Kh = Vg++;
    R.__alpha16[Kh] =
        Math.ceil(257.00392156862745 * Kh)
}
R.__clamp = new Uint8Array(511);
for (Vg = 0; 255 > Vg;) Kh = Vg++, R.__clamp[Kh] = Kh;
for (Vg = 255; 511 > Vg;) Kh = Vg++, R.__clamp[Kh] = 255;
Ga.level = 3;
"undefined" == typeof console && (console = {});
null == console.log && (console.log = function() {});
z.hitTestCanvas = qf.get_supported() ? window.document.createElement("canvas") : null;
z.hitTestContext = qf.get_supported() ? z.hitTestCanvas.getContext("2d") : null;
S.__meta__ = {
    fields: {
        __cairo: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        },
        addEventListener: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        },
        removeEventListener: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        }
    }
};
S.__broadcastEvents = new Qa;
S.__instanceCount = 0;
S.__tempStack = new nb(function() {
    return la.toObjectVector(null)
}, function(a) {
    a.set_length(0)
});
sb.MIN_WIDTH = 400;
sb.MIN_HEIGHT = 400;
sb.preview = !1;
ha.__fontByName = new Qa;
ha.__registeredFonts = [];
W.Element = 0;
W.PCData = 1;
W.CData = 2;
W.Comment = 3;
W.DocType = 4;
W.ProcessingInstruction = 5;
W.Document = 6;
q.offset = 10;
q.delay = 4;
u.windows = [];
D.black = 0;
D.dark = 4473924;
D.medium = 8947848;
D.light = 13421772;
D.white = 16777215;
D.uiFont = "_sans";
D.uiFontMono = "_typewriter";
D.smallSize = 20;
D.normalSize = 24;
xc.okCancel = ["OK", "Cancel"];
Id.maxFaceLength = 13;
Hc.lastTab = 0;
Wa.SCALE = 1;
Wa.CX = 0;
Wa.CY = 0;
I.__pool = new nb(function() {
    return new I
}, function(a) {
    a.setTo(0, 0)
});
Oa._p0 = new I(-819.2, 0);
Oa._p1 = new I(819.2, 0);
fc.lx = 0;
fc.ly = 0;
Ja.JOINTS = function(a) {
    a = new mc;
    a.h[2] = "round";
    a.h[1] = "miter";
    a.h[0] = "bevel";
    return a
}(this);
Ja.CAPS = function(a) {
    a = new mc;
    a.h[1] = "round";
    a.h[2] = "square";
    a.h[0] = "butt";
    return a
}(this);
Ja.BLEND_MODES = function(a) {
    a =
        new mc;
    a.h[10] = "normal";
    a.h[9] = "multiply";
    a.h[0] = "plus-lighter";
    return a
}(this);
Ja.gradients = [];
Ja.imports = [];
Ja.substituteFont = Ja.substituteGenerics;
Ic.__meta__ = {
    obj: {
        generic: null
    }
};
Qb.EDGE_STACK = new Uint32Array(512);
Qb.EPSILON = Math.pow(2, -52);
kd.PROCESSES = ["None", "Offset"];
kd.ROOF_STYLES = ["Plain", "Hip", "Gable"];
kd.DISPLAY_MODES = ["Block", "Lots", "Complex", "Hidden"];
kd.TOWER_SHAPES = ["Round", "Square", "Open"];
kd.FIELD_MODES = ["Furrows", "Plain", "Hidden"];
kd.DISTRICTS_MODE = ["Hidden", "Straight", "Curved",
    "Legend"
];
kd.LANDMARK_MODES = ["Hidden", "Icon", "Legend"];
ze.marks = [.5, .333, .666];
Rd.embeddedScanned = !1;
Rd.embedded = function(a) {
    a = new Qa;
    a.h.default_font = {
        name: "IM Fell Great Primer",
        url: "https://fonts.googleapis.com/css2?family=IM+Fell+Great+Primer&display=swap",
        generic: "serif"
    };
    return a
}(this);
Ma.dirs = function(a) {
    a = new pa;
    a.set(new I(1, 0), "east");
    a.set(new I(-1, 0), "west");
    a.set(new I(0, 1), "south");
    a.set(new I(0, -1), "north");
    return a
}(this);
hd.outlineNormal = !0;
hd.outlineSolid = !0;
K.strokeNormal = 1.6;
K.strokeThin = .5 * K.strokeNormal;
K.strokeThick = 2 * K.strokeNormal;
K.tintMethods = ["Spectrum", "Brightness", "Overlay"];
K.lineInvScale = 1;
K.thinLines = !0;
K.colorPaper = 13419960;
K.colorDark = 1710359;
K.colorRoof = 10854549;
K.colorWater = 8354417;
K.colorGreen = 10854291;
K.colorRoad = K.colorPaper;
K.colorWall = K.colorDark;
K.colorTree = 8354417;
K.colorLight = 13419960;
K.colorLabel = K.colorDark;
K.tintMethod = K.tintMethods[0];
K.tintStrength = 30;
K.weathering = 20;
gc.cache = [];
ng.drawNormalTower_unit = new I;
Ub.sizes = function(a) {
    a = new Qa;
    a.h.small = {
        min: 10,
        max: 20
    };
    a.h.medium = {
        min: 20,
        max: 40
    };
    a.h.large = {
        min: 40,
        max: 80
    };
    return a
}(this);
Ub.nextSize = 25;
pc.THICKNESS = 1.9;
pc.TOWER_RADIUS = 1.9;
pc.LTOWER_RADIUS = 2.5;
C.seed = 1;
C.saved = -1;
Se.permutation = [151, 160, 137, 91, 90, 15, 131, 13, 201, 95, 96, 53, 194, 233, 7, 225, 140, 36, 103, 30, 69, 142, 8, 99, 37, 240, 21, 10, 23, 190, 6, 148, 247, 120, 234, 75, 0, 26, 197, 62, 94, 252, 219, 203, 117, 35, 11, 32, 57, 177, 33, 88, 237, 149, 56, 87, 174, 20, 125, 136, 171, 168, 68, 175, 74, 165, 71, 134, 139, 48, 27, 166, 77, 146, 158, 231, 83, 111, 229, 122, 60, 211, 133, 230, 220, 105,
    92, 41, 55, 46, 245, 40, 244, 102, 143, 54, 65, 25, 63, 161, 1, 216, 80, 73, 209, 76, 132, 187, 208, 89, 18, 169, 200, 196, 135, 130, 116, 188, 159, 86, 164, 100, 109, 198, 173, 186, 3, 64, 52, 217, 226, 250, 124, 123, 5, 202, 38, 147, 118, 126, 255, 82, 85, 212, 207, 206, 59, 227, 47, 16, 58, 17, 182, 189, 28, 42, 223, 183, 170, 213, 119, 248, 152, 2, 44, 154, 163, 70, 221, 153, 101, 155, 167, 43, 172, 9, 129, 22, 39, 253, 19, 98, 108, 110, 79, 113, 224, 232, 178, 185, 112, 104, 218, 246, 97, 228, 251, 34, 242, 193, 238, 210, 144, 12, 191, 179, 162, 241, 81, 51, 145, 235, 249, 14, 239, 107, 49, 192, 214, 31, 181, 199, 106, 157, 184,
    84, 204, 176, 115, 121, 50, 45, 127, 4, 150, 254, 138, 236, 205, 93, 222, 114, 67, 29, 24, 72, 243, 141, 128, 195, 78, 66, 215, 61, 156, 180
];
Ae.pattern = new dk(30, 30, 2.25);
Ae.noise = pg.fractal(5, .05);
Bb.newModel = new ec;
Bb.titleChanged = new ec;
Bb.geometryChanged = new ec;
Bb.districtsChanged = new Nc;
Bb.landmarksChanged = new Nc;
Bb.unitsChanged = new Nc;
Db.meters = new Db("m", .25);
Db.yards = new Db("yd", .2286);
Db.metric = new Db("km", 250, Db.meters);
Db.imperial = new Db("mi", 402.336, Db.yards);
Db._current = Db.metric;
yd.MIN_SUBPLOT = 400;
yd.MIN_FURROW =
    1.3;
Te.DWELLINGS_URL = "https://watabou.github.io/dwellings/";
vc.saved = {};
Kd.features = "Citadel;citadel;Temple;temple;Inner castle;urban_castle;Plaza;plaza;Walls;walls;Shanty town;shantytown;River;river;Coast;coast;Greens;green".split(";");
Kd.nonRandom = ["Greens", "Farms"];
ke.lastTab = 0;
Ec.toggleBuildings_displayMode = "Lots";
Ec.tools = [Kd, rg, ke];
Fb.__meta__ = {
    fields: {
        image: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        },
        __framebufferContext: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        },
        __indexBufferContext: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        },
        __surface: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        },
        __textureContext: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        },
        __vertexBufferContext: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        },
        __fromImage: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        }
    }
};
Fb.__tempVector = new jg;
na.__pool = new nb(function() {
    return new na
}, function(a) {
    a.setTo(0, 0, 0, 0)
});
Ad.__current = new Ad(E.location.href);
Ad.__isWorker = "undefined" == typeof window;
Ad.__messages = new ab;
Ad.__resolveMethods = new ab;
ua.__meta__ = {
    fields: {
        equals: {
            SuppressWarnings: ["checkstyle:FieldDocComment"]
        }
    }
};
ua.__identity = new ua;
ua.__pool = new nb(function() {
    return new ua
}, function(a) {
    a.identity()
});
Tb.__pool = new nb(function() {
    return new Tb
}, function(a) {
    a.__identity()
});
jd.pixel = new Fb(1, 1, !1, 16711680);
jd.cache = [];
tb.o = new I;
tb.convexity = 0;
tb.secondary = 0;
tb.mainRing = 0;
tb.auxRing = 0;
tb.north = 0;
tb.south = 0;
Ca.MARGIN = 10;
qg.RADIUS = 80;
ra.ARMORIA = "https://armoria.herokuapp.com";
ra.LO_RES = 200;
ra.HI_RES = 800;
ra.updated = new Nc;
ra.setHiRes = new ec;
ra.loading = !1;
nf.pos = yc.BOTTOM_LEFT;
Uc.pos = yc.TOP_LEFT;
Uc.auto = !0;
me.brushRadius =
    20;
Kf.brushRadius = 20;
Lf.brushRadius = 20;
Mf.brushRadius = 20;
vb.fontTitle = {
    face: null,
    embedded: "default_font",
    size: 72,
    bold: !1,
    italic: !1
};
vb.fontLabel = {
    face: null,
    embedded: "default_font",
    size: 36,
    bold: !1,
    italic: !1
};
vb.fontLegend = {
    face: null,
    embedded: "default_font",
    size: 28,
    bold: !1,
    italic: !1
};
vb.fontPin = {
    face: null,
    embedded: "default_font",
    size: 28,
    bold: !1,
    italic: !1
};
vb.fontElement = {
    face: null,
    embedded: "default_font",
    size: 28,
    bold: !1,
    italic: !1
};
Nf.EDITOR = "https://azgaar.github.io/Armoria";
nd.VOWELS = "you ye yo ya ie ee oo ea ei ey oi ou ai ay au oi oy ue ua u o a e i y".split(" ");
nd.CONSONANTS = "wh th ck ch sh gh ph qu b c d f g h j k l m n p q r s t v w x z".split(" ");
za.baseURL = "https://watabou.github.io/city-generator/0.10.0/";
jb.plurals = function(a) {
    a = new Qa;
    a.h.child = "children";
    a.h.fish = "fish";
    return a
}(this);
Oe.rng = Math.random;
Sh.values = function(a) {
    a = new Qa;
    a.h.aliceblue = 15792383;
    a.h.antiquewhite = 16444375;
    a.h.aqua = 65535;
    a.h.aquamarine = 8388564;
    a.h.azure = 15794175;
    a.h.beige = 16119260;
    a.h.bisque = 16770244;
    a.h.black = 0;
    a.h.blanchedalmond = 16772045;
    a.h.blue = 255;
    a.h.blueviolet =
        9055202;
    a.h.brown = 10824234;
    a.h.burlywood = 14596231;
    a.h.cadetblue = 6266528;
    a.h.chartreuse = 8388352;
    a.h.chocolate = 13789470;
    a.h.coral = 16744272;
    a.h.cornflowerblue = 6591981;
    a.h.cornsilk = 16775388;
    a.h.crimson = 14423100;
    a.h.cyan = 65535;
    a.h.darkblue = 139;
    a.h.darkcyan = 35723;
    a.h.darkgoldenrod = 12092939;
    a.h.darkgreen = 25600;
    a.h.darkgrey = 11119017;
    a.h.darkkhaki = 12433259;
    a.h.darkmagenta = 9109643;
    a.h.darkolivegreen = 5597999;
    a.h.darkorange = 16747520;
    a.h.darkorchid = 10040012;
    a.h.darkred = 9109504;
    a.h.darksalmon = 15308410;
    a.h.darkseagreen =
        9419919;
    a.h.darkslateblue = 4734347;
    a.h.darkslategrey = 3100495;
    a.h.darkturquoise = 52945;
    a.h.darkviolet = 9699539;
    a.h.deeppink = 16716947;
    a.h.deepskyblue = 49151;
    a.h.dimgrey = 6908265;
    a.h.dodgerblue = 2003199;
    a.h.firebrick = 11674146;
    a.h.floralwhite = 16775920;
    a.h.forestgreen = 2263842;
    a.h.fuchsia = 16711935;
    a.h.gainsboro = 14474460;
    a.h.ghostwhite = 16316671;
    a.h.goldenrod = 14329120;
    a.h.gold = 16766720;
    a.h.green = 32768;
    a.h.greenyellow = 11403055;
    a.h.grey = 8421504;
    a.h.honeydew = 15794160;
    a.h.hotpink = 16738740;
    a.h.indianred = 13458524;
    a.h.indigo =
        4915330;
    a.h.ivory = 16777200;
    a.h.khaki = 15787660;
    a.h.lavenderblush = 16773365;
    a.h.lavender = 15132410;
    a.h.lawngreen = 8190976;
    a.h.lemonchiffon = 16775885;
    a.h.lightblue = 11393254;
    a.h.lightcoral = 15761536;
    a.h.lightcyan = 14745599;
    a.h.lightgoldenrodyellow = 16448210;
    a.h.lightgreen = 9498256;
    a.h.lightgrey = 13882323;
    a.h.lightpink = 16758465;
    a.h.lightsalmon = 16752762;
    a.h.lightseagreen = 2142890;
    a.h.lightskyblue = 8900346;
    a.h.lightslategrey = 7833753;
    a.h.lightsteelblue = 11584734;
    a.h.lightyellow = 16777184;
    a.h.lime = 65280;
    a.h.limegreen =
        3329330;
    a.h.linen = 16445670;
    a.h.magenta = 16711935;
    a.h.maroon = 8388608;
    a.h.mediumaquamarine = 6737322;
    a.h.mediumblue = 205;
    a.h.mediumorchid = 12211667;
    a.h.mediumpurple = 9662683;
    a.h.mediumseagreen = 3978097;
    a.h.mediumslateblue = 8087790;
    a.h.mediumspringgreen = 64154;
    a.h.mediumturquoise = 4772300;
    a.h.mediumvioletred = 13047173;
    a.h.midnightblue = 1644912;
    a.h.mintcream = 16121850;
    a.h.mistyrose = 16770273;
    a.h.moccasin = 16770229;
    a.h.navajowhite = 16768685;
    a.h.navy = 128;
    a.h.oldlace = 16643558;
    a.h.olive = 8421376;
    a.h.olivedrab = 7048739;
    a.h.orange =
        16753920;
    a.h.orangered = 16729344;
    a.h.orchid = 14315734;
    a.h.palegoldenrod = 15657130;
    a.h.palegreen = 10025880;
    a.h.paleturquoise = 11529966;
    a.h.palevioletred = 14381203;
    a.h.papayawhip = 16773077;
    a.h.peachpuff = 16767673;
    a.h.peru = 13468991;
    a.h.pink = 16761035;
    a.h.plum = 14524637;
    a.h.powderblue = 11591910;
    a.h.purple = 8388736;
    a.h.rebeccapurple = 6697881;
    a.h.red = 16711680;
    a.h.rosybrown = 12357519;
    a.h.royalblue = 4286945;
    a.h.saddlebrown = 9127187;
    a.h.salmon = 16416882;
    a.h.sandybrown = 16032864;
    a.h.seagreen = 3050327;
    a.h.seashell = 16774638;
    a.h.sienna =
        10506797;
    a.h.silver = 12632256;
    a.h.skyblue = 8900331;
    a.h.slateblue = 6970061;
    a.h.slategrey = 7372944;
    a.h.snow = 16775930;
    a.h.springgreen = 65407;
    a.h.steelblue = 4620980;
    a.h.tan = 13808780;
    a.h.teal = 32896;
    a.h.thistle = 14204888;
    a.h.tomato = 16737095;
    a.h.turquoise = 4251856;
    a.h.violet = 15631086;
    a.h.wheat = 16113331;
    a.h.white = 16777215;
    a.h.whitesmoke = 16119285;
    a.h.yellow = 16776960;
    a.h.yellowgreen = 10145074;
    return a
}(this);
rb._tick = new ec;
rb.lastTime = 0;
rb.timeScale = 1;
Bd.USE_CACHE = !1;
Bd.USE_ENUM_INDEX = !1;
Bd.BASE64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789%:";
pd.DEFAULT_RESOLVER = new Ji;
pd.BASE64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789%:";
ef.escapes = function(a) {
    a = new Qa;
    a.h.lt = "<";
    a.h.gt = ">";
    a.h.amp = "&";
    a.h.quot = '"';
    a.h.apos = "'";
    return a
}(this);
ad.LEN_EXTRA_BITS_TBL = [0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0, -1, -1];
ad.LEN_BASE_VAL_TBL = [3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258];
ad.DIST_EXTRA_BITS_TBL = [0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, -1,
    -1
];
ad.DIST_BASE_VAL_TBL = [1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577];
ad.CODE_LENGTHS_POS = [16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15];
Ba.activeRequests = 0;
Ba.requestLimit = 17;
Ba.requestQueue = new ab;
Ha.dummyCharacter = "\u007f";
Ha.windowID = 0;
Be.DICTIONARY = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/".split("");
Be.EXTENDED_DICTIONARY = function(a) {
    a = [];
    for (var b = 0, c = Be.DICTIONARY; b < c.length;) {
        var d = c[b];
        ++b;
        for (var f =
                0, h = Be.DICTIONARY; f < h.length;) {
            var k = h[f];
            ++f;
            a.push(d + k)
        }
    }
    return a
}(this);
Cd.MUL_TABLE = [1, 171, 205, 293, 57, 373, 79, 137, 241, 27, 391, 357, 41, 19, 283, 265, 497, 469, 443, 421, 25, 191, 365, 349, 335, 161, 155, 149, 9, 278, 269, 261, 505, 245, 475, 231, 449, 437, 213, 415, 405, 395, 193, 377, 369, 361, 353, 345, 169, 331, 325, 319, 313, 307, 301, 37, 145, 285, 281, 69, 271, 267, 263, 259, 509, 501, 493, 243, 479, 118, 465, 459, 113, 446, 55, 435, 429, 423, 209, 413, 51, 403, 199, 393, 97, 3, 379, 375, 371, 367, 363, 359, 355, 351, 347, 43, 85, 337, 333, 165, 327, 323, 5, 317, 157, 311, 77, 305, 303,
    75, 297, 294, 73, 289, 287, 71, 141, 279, 277, 275, 68, 135, 67, 133, 33, 262, 260, 129, 511, 507, 503, 499, 495, 491, 61, 121, 481, 477, 237, 235, 467, 232, 115, 457, 227, 451, 7, 445, 221, 439, 218, 433, 215, 427, 425, 211, 419, 417, 207, 411, 409, 203, 202, 401, 399, 396, 197, 49, 389, 387, 385, 383, 95, 189, 47, 187, 93, 185, 23, 183, 91, 181, 45, 179, 89, 177, 11, 175, 87, 173, 345, 343, 341, 339, 337, 21, 167, 83, 331, 329, 327, 163, 81, 323, 321, 319, 159, 79, 315, 313, 39, 155, 309, 307, 153, 305, 303, 151, 75, 299, 149, 37, 295, 147, 73, 291, 145, 289, 287, 143, 285, 71, 141, 281, 35, 279, 139, 69, 275, 137, 273, 17,
    271, 135, 269, 267, 133, 265, 33, 263, 131, 261, 130, 259, 129, 257, 1
];
Cd.SHG_TABLE = [0, 9, 10, 11, 9, 12, 10, 11, 12, 9, 13, 13, 10, 9, 13, 13, 14, 14, 14, 14, 10, 13, 14, 14, 14, 13, 13, 13, 9, 14, 14, 14, 15, 14, 15, 14, 15, 15, 14, 15, 15, 15, 14, 15, 15, 15, 15, 15, 14, 15, 15, 15, 15, 15, 15, 12, 14, 15, 15, 13, 15, 15, 15, 15, 16, 16, 16, 15, 16, 14, 16, 16, 14, 16, 13, 16, 16, 16, 15, 16, 13, 16, 15, 16, 14, 9, 16, 16, 16, 16, 16, 16, 16, 16, 16, 13, 14, 16, 16, 15, 16, 16, 10, 16, 15, 16, 14, 16, 16, 14, 16, 16, 14, 16, 16, 14, 15, 16, 16, 16, 14, 15, 14, 15, 13, 16, 16, 15, 17, 17, 17, 17, 17, 17, 14, 15, 17, 17, 16, 16, 17, 16, 15, 17, 16, 17,
    11, 17, 16, 17, 16, 17, 16, 17, 17, 16, 17, 17, 16, 17, 17, 16, 16, 17, 17, 17, 16, 14, 17, 17, 17, 17, 15, 16, 14, 16, 15, 16, 13, 16, 15, 16, 14, 16, 15, 16, 12, 16, 15, 16, 17, 17, 17, 17, 17, 13, 16, 15, 17, 17, 17, 16, 15, 17, 17, 17, 16, 15, 17, 17, 14, 16, 17, 17, 16, 17, 17, 16, 15, 17, 16, 14, 17, 16, 15, 17, 16, 17, 17, 16, 17, 15, 16, 17, 14, 17, 16, 15, 17, 16, 17, 13, 17, 16, 17, 17, 16, 17, 14, 17, 16, 17, 16, 17, 16, 17, 9
];
pb.__identity = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
zc.onUpdate = new Ac;
zc.__updated = !1;
He.sensorByID = new mc;
He.sensors = [];
qc.devices = new mc;
qc.onConnect = new zk;
nc.devices = new mc;
nc.onConnect = new Ak;
dc.onCancel = new Fg;
dc.onEnd = new Fg;
dc.onMove = new Fg;
dc.onStart = new Fg;
Fa.cache = new Dk;
Fa.onChange = new Ac;
Fa.bundlePaths = new Qa;
Fa.libraries = new Qa;
Fa.libraryPaths = new Qa;
Ga.throwErrors = !0;
Ra.__lastTimerID = 0;
Ra.__timers = new mc;
Ye.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    }
};
$i.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    },
    fields: {
        toJSON: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        }
    }
};
Ie.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    },
    fields: {
        toJSON: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        },
        _: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        }
    }
};
aj.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    },
    fields: {
        toJSON: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        }
    }
};
Zg.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    },
    fields: {
        toJSON: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        }
    }
};
fg.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    },
    fields: {
        toJSON: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        },
        _: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        }
    }
};
Wg.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    }
};
qd.__meta__ = {
    fields: {
        __context: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        },
        __type: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        }
    }
};
bj.__meta__ = {
    fields: {
        cairo: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        },
        __matrix3: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        },
        applyMatrix: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        },
        __setBlendModeCairo: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        },
        _: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        }
    }
};
Ue.__meta__ = {
    fields: {
        context: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        },
        applySmoothing: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        },
        setTransform: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        },
        __setBlendModeContext: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        },
        _: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        }
    }
};
fj.__meta__ = {
    fields: {
        __element: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        },
        _: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        }
    }
};
xh.__meta__ = {
    fields: {
        element: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        },
        _: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        }
    }
};
Dd.__meta__ = {
    fields: {
        glProgram: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        }
    }
};
te.__meta__ = {
    statics: {
        create: {
            SuppressWarnings: ["checkstyle:FieldDocComment"]
        }
    },
    fields: {
        parameters: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        }
    }
};
te.__rootURL = qf.get_supported() ? window.document.URL : "";
db.__meta__ = {
    fields: {
        gl: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        },
        __gl: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        },
        __matrix: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        },
        __projection: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        },
        __projectionFlipped: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        }
    }
};
db.__alphaValue = [1];
db.__colorMultipliersValue = [0, 0, 0, 0];
db.__colorOffsetsValue = [0, 0, 0, 0];
db.__emptyColorValue = [0, 0, 0, 0];
db.__hasColorTransformValue = [!1];
db.__scissorRectangle = new na;
db.__textureSizeValue = [0, 0];
Lh.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    },
    fields: {
        onComplete: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        }
    }
};
Xg.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    }
};
ij.__meta__ = {
    fields: {
        index: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        },
        name: {
            SuppressWarnings: ["checkstyle:FieldDocComment"]
        }
    }
};
Ig.__meta__ = {
    fields: {
        index: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        },
        name: {
            SuppressWarnings: ["checkstyle:FieldDocComment"]
        }
    }
};
Lg.__meta__ = {
    fields: {
        __broadcastEvent: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        },
        __dispatchEvent: {
            SuppressWarnings: [
                ["checkstyle:Dynamic", "checkstyle:LeftCurly"]
            ]
        },
        __dispatchStack: {
            SuppressWarnings: [
                ["checkstyle:Dynamic", "checkstyle:LeftCurly"]
            ]
        },
        __dispatchTarget: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        },
        __handleError: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        }
    }
};
xj.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    }
};
vh.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    },
    fields: {
        _: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        }
    }
};
wh.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    }
};
Uf.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    }
};
sd.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    },
    fields: {
        o: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        }
    }
};
sd.empty = new sd;
z.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    },
    statics: {
        windingRule: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        },
        createBitmapFill: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        },
        createGradientPattern: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        }
    }
};
z.fillCommands = new sd;
z.strokeCommands = new sd;
Q.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    }
};
Ze.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    },
    statics: {
        renderTileContainer: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        }
    }
};
Vf.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    }
};
Vd.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    }
};
Dh.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    }
};
Yf.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    }
};
Yb.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    }
};
Yb.blankBitmapData = new Fb(1, 1, !1, 0);
Yb.tempColorTransform = new Tb(1, 1, 1, 1, 0, 0, 0, 0);
Xf.opaqueBitmapData = new Fb(1, 1, !1, 0);
Ee.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    }
};
bf.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    }
};
V.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    }
};
Wd.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    }
};
Wd.__textureSizeValue = [0, 0];
Zb.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    }
};
rd.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    }
};
wf.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    }
};
uf.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    }
};
vf.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    }
};
$e.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    }
};
ue.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    }
};
bg.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    }
};
kj.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    },
    fields: {
        overrideIntValues: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        }
    }
};
ub.__glDepthStencil = -1;
ub.__glMaxTextureMaxAnisotropy = -1;
ub.__glMaxViewportDims = -1;
ub.__glMemoryCurrentAvailable = -1;
ub.__glMemoryTotalAvailable = -1;
ub.__glTextureMaxAnisotropy = -1;
Bj.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    }
};
Gh.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    }
};
bd.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    }
};
bd.limitedProfile = !0;
Mg.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    }
};
Cj.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    }
};
Ng.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    }
};
cg.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    }
};
Eh.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    }
};
Eb.__meta__ = {
    fields: {
        __textureContext: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        },
        __getGLFramebuffer: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        }
    }
};
Yg.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    }
};
Bf.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    }
};
Bf.supportsWeakReference = Object.prototype.hasOwnProperty.call(E, "WeakRef");
Sc.__invertAlphaShader = new Lj;
Sc.__blurAlphaShader = new Gj;
Sc.__combineShader = new Ij;
Sc.__innerCombineShader =
    new Kj;
Sc.__combineKnockoutShader = new Hj;
Sc.__innerCombineKnockoutShader = new Jj;
dg.__meta__ = {
    fields: {
        clone: {
            SuppressWarnings: ["checkstyle:FieldDocComment"]
        }
    }
};
ve.__soundChannels = [];
ve.__soundTransform = new dg;
$c.defaultObjectEncoding = 10;
xf.followRedirects = !0;
xf.idleTimeout = 0;
xf.manageCookies = !0;
Jg.currentDomain = new Jg(null);
we.__meta__ = {
    fields: {
        clone: {
            SuppressWarnings: ["checkstyle:FieldDocComment"]
        }
    }
};
Jh.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    }
};
La.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    }
};
La.__regexAlign = new ja("align\\s?=\\s?(\"([^\"]+)\"|'([^']+)')", "i");
La.__regexBreakTag = new ja("<br\\s*/?>", "gi");
La.__regexBlockIndent = new ja("blockindent\\s?=\\s?(\"([^\"]+)\"|'([^']+)')", "i");
La.__regexClass = new ja("class\\s?=\\s?(\"([^\"]+)\"|'([^']+)')", "i");
La.__regexColor = new ja("color\\s?=\\s?(\"#([^\"]+)\"|'#([^']+)')", "i");
La.__regexEntityApos = new ja("&apos;", "g");
La.__regexEntityNbsp = new ja("&nbsp;", "g");
La.__regexCharEntity = new ja("&#(?:([0-9]+)|(x[0-9a-fA-F]+));", "g");
La.__regexFace =
    new ja("face\\s?=\\s?(\"([^\"]+)\"|'([^']+)')", "i");
La.__regexHTMLTag = new ja("<.*?>", "g");
La.__regexHref = new ja("href\\s?=\\s?(\"([^\"]+)\"|'([^']+)')", "i");
La.__regexIndent = new ja(" indent\\s?=\\s?(\"([^\"]+)\"|'([^']+)')", "i");
La.__regexLeading = new ja("leading\\s?=\\s?(\"([^\"]+)\"|'([^']+)')", "i");
La.__regexLeftMargin = new ja("leftmargin\\s?=\\s?(\"([^\"]+)\"|'([^']+)')", "i");
La.__regexRightMargin = new ja("rightmargin\\s?=\\s?(\"([^\"]+)\"|'([^']+)')", "i");
La.__regexSize = new ja("size\\s?=\\s?(\"([^\"]+)\"|'([^']+)')",
    "i");
La.__regexTabStops = new ja("tabstops\\s?=\\s?(\"([^\"]+)\"|'([^']+)')", "i");
Ug.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    }
};
Nb.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    }
};
Mj.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    }
};
qb.__meta__ = {
    fields: {
        addEventListener: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        }
    }
};
qb.numDevices = 0;
qb.__deviceList = [];
qb.__instances = [];
qb.__devices = new pa;
Ik.__cursor = "auto";
ma.__meta__ = {
    obj: {
        SuppressWarnings: [
            ["checkstyle:ConstantName",
                "checkstyle:FieldDocComment"
            ]
        ]
    }
};
ma.OPMAP = new Qa;
ma.REGMAP = new Qa;
ma.SAMPLEMAP = new Qa;
ma.initialized = !1;
Ya.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    }
};
Md.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    }
};
$b.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    }
};
ac.cache = new Oj;
Vc.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    }
};
Vc.defaultObjectEncoding = 10;
Sd.__meta__ = {
    statics: {
        __get: {
            SuppressWarnings: ["checkstyle:FieldDocComment"]
        }
    }
};
Pj.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    }
};
Qj.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    }
};
Dc.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    }
};
ag.__meta__ = {
    obj: {
        SuppressWarnings: ["checkstyle:FieldDocComment"]
    },
    fields: {
        touch: {
            SuppressWarnings: ["checkstyle:Dynamic"]
        }
    }
};
ag.__pool = new nb(function() {
    return new ag
}, function(a) {
    a.reset()
});
aa.main()
})("undefined" != typeof t ? t : "undefined" != typeof window ? window : "undefined" != typeof self ? self : this, "undefined" !=
    typeof window ? window : "undefined" != typeof E ? E : "undefined" != typeof self ? self : this)
};
"undefined" !== typeof self && self.constructor.name.includes("Worker") ? E({}, t) : (A.lime = A.lime || {}, A.lime.$scripts = A.lime.$scripts || {}, A.lime.$scripts.mfcg = E, A.lime.embed = function(E) {
    var B = {},
        M = A.lime.$scripts[E];
    if (!M) throw Error('Cannot find project name "' + E + '"');
    M(B, t);
    for (var aa in B) A[aa] = A[aa] || B[aa];
    (M = B.lime || window.lime) && M.embed && this !== M.embed && M.embed.apply(M, arguments);
    return B
});
"function" === typeof define &&
    define.amd && (define([], function() {
        return A.lime
    }), define.__amd = define.amd, define.amd = null)
};
$lime_init("undefined" !== typeof exports ? exports : "function" === typeof define && define.amd ? {} : "undefined" !== typeof window ? window : "undefined" !== typeof self ? self : this, "undefined" !== typeof window ? window : "undefined" !== typeof global ? global : "undefined" !== typeof self ? self : this);
