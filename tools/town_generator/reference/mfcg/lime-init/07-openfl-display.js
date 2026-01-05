/*
 * lime-init/07-openfl-display.js
 * Part 7/8: OpenFL Display Objects
 * Contains: openfl.display.*, openfl.text.*, graphics rendering internals
 */
    }
};
dj.renderDrawableMask = function(a, b) {
    for (var c = a.__removedChildren.iterator(); c.hasNext();) {
        var d = c.next();
        null == d.stage && d.__cleanup()
    }
    a.__removedChildren.set_length(0);
    null != a.__graphics && z.renderMask(a.__graphics, b);
    c = 0;
    for (a =
        a.__children; c < a.length;) d = a[c], ++c, b.__renderDrawableMask(d)
};
var sd = function() {
    null == sd.empty ? (this.types = [], this.b = [], this.i = [], this.f = [], this.o = [], this.ff = [], this.ii = [], this.copyOnWrite = !0) : this.clear()
};
g["openfl.display._internal.DrawCommandBuffer"] = sd;
sd.__name__ = "openfl.display._internal.DrawCommandBuffer";
sd.prototype = {
    beginBitmapFill: function(a, b, c, d) {
        this.prepareWrite();
        this.types.push(da.BEGIN_BITMAP_FILL);
        this.o.push(a);
        this.o.push(b);
        this.b.push(c);
        this.b.push(d)
    },
    beginFill: function(a,
        b) {
        this.prepareWrite();
        this.types.push(da.BEGIN_FILL);
        this.i.push(a);
        this.f.push(b)
    },
    beginGradientFill: function(a, b, c, d, f, h, k, n) {
        this.prepareWrite();
        this.types.push(da.BEGIN_GRADIENT_FILL);
        this.o.push(a);
        this.ii.push(b);
        this.ff.push(c);
        this.ii.push(d);
        this.o.push(f);
        this.o.push(h);
        this.o.push(k);
        this.f.push(n)
    },
    beginShaderFill: function(a) {
        this.prepareWrite();
        this.types.push(da.BEGIN_SHADER_FILL);
        this.o.push(a)
    },
    clear: function() {
        this.types = sd.empty.types;
        this.b = sd.empty.b;
        this.i = sd.empty.i;
        this.f =
            sd.empty.f;
        this.o = sd.empty.o;
        this.ff = sd.empty.ff;
        this.ii = sd.empty.ii;
        this.copyOnWrite = !0
    },
    cubicCurveTo: function(a, b, c, d, f, h) {
        this.prepareWrite();
        this.types.push(da.CUBIC_CURVE_TO);
        this.f.push(a);
        this.f.push(b);
        this.f.push(c);
        this.f.push(d);
        this.f.push(f);
        this.f.push(h)
    },
    curveTo: function(a, b, c, d) {
        this.prepareWrite();
        this.types.push(da.CURVE_TO);
        this.f.push(a);
        this.f.push(b);
        this.f.push(c);
        this.f.push(d)
    },
    drawCircle: function(a, b, c) {
        this.prepareWrite();
        this.types.push(da.DRAW_CIRCLE);
        this.f.push(a);
        this.f.push(b);
        this.f.push(c)
    },
    drawEllipse: function(a, b, c, d) {
        this.prepareWrite();
        this.types.push(da.DRAW_ELLIPSE);
        this.f.push(a);
        this.f.push(b);
        this.f.push(c);
        this.f.push(d)
    },
    drawQuads: function(a, b, c) {
        this.prepareWrite();
        this.types.push(da.DRAW_QUADS);
        this.o.push(a);
        this.o.push(b);
        this.o.push(c)
    },
    drawRect: function(a, b, c, d) {
        this.prepareWrite();
        this.types.push(da.DRAW_RECT);
        this.f.push(a);
        this.f.push(b);
        this.f.push(c);
        this.f.push(d)
    },
    drawRoundRect: function(a, b, c, d, f, h) {
        this.prepareWrite();
        this.types.push(da.DRAW_ROUND_RECT);
        this.f.push(a);
        this.f.push(b);
        this.f.push(c);
        this.f.push(d);
        this.f.push(f);
        this.o.push(h)
    },
    drawTriangles: function(a, b, c, d) {
        this.prepareWrite();
        this.types.push(da.DRAW_TRIANGLES);
        this.o.push(a);
        this.o.push(b);
        this.o.push(c);
        this.o.push(d)
    },
    endFill: function() {
        this.prepareWrite();
        this.types.push(da.END_FILL)
    },
    lineBitmapStyle: function(a, b, c, d) {
        this.prepareWrite();
        this.types.push(da.LINE_BITMAP_STYLE);
        this.o.push(a);
        this.o.push(b);
        this.b.push(c);
        this.b.push(d)
    },
    lineGradientStyle: function(a, b, c, d, f, h, k,
        n) {
        this.prepareWrite();
        this.types.push(da.LINE_GRADIENT_STYLE);
        this.o.push(a);
        this.ii.push(b);
        this.ff.push(c);
        this.ii.push(d);
        this.o.push(f);
        this.o.push(h);
        this.o.push(k);
        this.f.push(n)
    },
    lineStyle: function(a, b, c, d, f, h, k, n) {
        this.prepareWrite();
        this.types.push(da.LINE_STYLE);
        this.o.push(a);
        this.i.push(b);
        this.f.push(c);
        this.b.push(d);
        this.o.push(f);
        this.o.push(h);
        this.o.push(k);
        this.f.push(n)
    },
    lineTo: function(a, b) {
        this.prepareWrite();
        this.types.push(da.LINE_TO);
        this.f.push(a);
        this.f.push(b)
    },
    moveTo: function(a,
        b) {
        this.prepareWrite();
        this.types.push(da.MOVE_TO);
        this.f.push(a);
        this.f.push(b)
    },
    prepareWrite: function() {
        this.copyOnWrite && (this.types = this.types.slice(), this.b = this.b.slice(), this.i = this.i.slice(), this.f = this.f.slice(), this.o = this.o.slice(), this.ff = this.ff.slice(), this.ii = this.ii.slice(), this.copyOnWrite = !1)
    },
    windingEvenOdd: function() {
        this.prepareWrite();
        this.types.push(da.WINDING_EVEN_ODD)
    },
    windingNonZero: function() {
        this.prepareWrite();
        this.types.push(da.WINDING_NON_ZERO)
    },
    get_length: function() {
        return this.types.length
    },
    __class__: sd,
    __properties__: {
        get_length: "get_length"
    }
};
var z = function() {};
g["openfl.display._internal.CanvasGraphics"] = z;
z.__name__ = "openfl.display._internal.CanvasGraphics";
z.closePath = function(a) {
    null == a && (a = !1);
    null != z.context.strokeStyle && (a || z.context.closePath(), z.context.stroke(), a && z.context.closePath(), z.context.beginPath())
};
z.createBitmapFill = function(a, b, c) {
    Ka.convertToCanvas(a.image);
    z.setSmoothing(c);
    return z.context.createPattern(a.image.get_src(), b ? "repeat" : "no-repeat")
};
z.createGradientPattern =
    function(a, b, c, d, f, h, k, n) {
        k = !1;
        null == f && (f = ua.__pool.get(), f.identity(), k = !0);
        switch (a) {
            case 0:
                if (0 == h) {
                    a = z.context.createLinearGradient(-819.2, 0, 819.2, 0);
                    z.pendingMatrix = f.clone();
                    z.inversePendingMatrix = f.clone();
                    z.inversePendingMatrix.invert();
                    for (var g = 0, q = b.length; g < q;) {
                        var u = g++;
                        h = d[u] / 255;
                        0 > h ? h = 0 : 1 < h && (h = 1);
                        a.addColorStop(h, z.getRGBA(b[u], c[u]))
                    }
                    k && ua.__pool.release(f);
                    return a
                }
                a = 819.2 * ((0 == h ? 1 : 25) - 1);
                k = window.document.createElement("canvas");
                n = k.getContext("2d");
                var m = z.getDimensions(f);
                k.width =
                    z.context.canvas.width;
                k.height = z.context.canvas.height;
                a = z.context.createLinearGradient(-819.2 - a, 0, 819.2 + a, 0);
                if (1 == h)
                    for (var r = 0, l = .04; 1 > r;) {
                        g = 0;
                        for (q = b.length; g < q;) u = g++, h = d[u] / 255, h = r + h * l, 0 > h ? h = 0 : 1 < h && (h = 1), a.addColorStop(h, z.getRGBA(b[u], c[u]));
                        r += l;
                        for (g = b.length - 1; 0 <= g;) h = d[g] / 255, h = r + (1 - h) * l, 0 > h ? h = 0 : 1 < h && (h = 1), a.addColorStop(h, z.getRGBA(b[g], c[g])), --g;
                        r += l
                    } else if (2 == h)
                        for (r = 0, l = .04; 1 > r;) {
                            g = 0;
                            for (q = b.length; g < q;) u = g++, h = d[u] / 255, h = r + h * l, 0 > h ? h = 0 : 1 < h && (h = .999), a.addColorStop(h, z.getRGBA(b[u],
                                c[u]));
                            h = r + .001;
                            0 > h ? h = 0 : 1 < h && (h = 1);
                            a.addColorStop(h - .001, z.getRGBA(b[b.length - 1], c[c.length - 1]));
                            a.addColorStop(h, z.getRGBA(b[0], c[0]));
                            r += l
                        }
                z.pendingMatrix = new ua;
                z.pendingMatrix.tx = f.tx - m.width / 2;
                z.pendingMatrix.ty = f.ty - m.height / 2;
                z.inversePendingMatrix = z.pendingMatrix.clone();
                z.inversePendingMatrix.invert();
                b = new Path2D;
                b.rect(0, 0, k.width, k.height);
                b.closePath();
                f = new DOMMatrix([f.a, f.b, f.c, f.d, f.tx, f.ty]);
                c = f.inverse();
                d = new Path2D;
                d.addPath(b, c);
                n.fillStyle = a;
                n.setTransform(f);
                n.fill(d);
                return z.context.createPattern(k,
                    "no-repeat");
            case 1:
                1 < n ? n = 1 : -1 > n && (n = -1);
                a = z.context.createRadialGradient(819.2 * n, 0, 0, 0, 0, 819.2);
                z.pendingMatrix = f.clone();
                z.inversePendingMatrix = f.clone();
                z.inversePendingMatrix.invert();
                g = 0;
                for (q = b.length; g < q;) u = g++, h = d[u] / 255, 0 > h ? h = 0 : 1 < h && (h = 1), a.addColorStop(h, z.getRGBA(b[u], c[u]));
                k && ua.__pool.release(f);
                return a
        }
    };
z.getRGBA = function(a, b) {
    var c = (a & 16711680) >>> 16,
        d = (a & 65280) >>> 8;
    a &= 255;
    return "rgba(" + (null == c ? "null" : H.string(cb.toFloat(c))) + ", " + (null == d ? "null" : H.string(cb.toFloat(d))) + ", " + (null ==
        a ? "null" : H.string(cb.toFloat(a))) + ", " + b + ")"
};
z.getDimensions = function(a) {
    var b = Math.cos(Math.atan2(a.c, a.a)),
        c = a.a / b * 1638.4;
    a = a.d / b * 1638.4;
    0 == c && 0 == a && (c = a = 819.2);
    return {
        width: c,
        height: a
    }
};
z.createTempPatternCanvas = function(a, b, c, d) {
    var f = window.document.createElement("canvas"),
        h = f.getContext("2d");
    f.width = c;
    f.height = d;
    a = a.image.get_src();
    h.fillStyle = h.createPattern(a, b ? "repeat" : "no-repeat");
    h.beginPath();
    h.moveTo(0, 0);
    h.lineTo(0, d);
    h.lineTo(c, d);
    h.lineTo(c, 0);
    h.lineTo(0, 0);
    h.closePath();
    z.hitTesting ||
        h.fill(z.windingRule);
    return f
};
z.drawRoundRect = function(a, b, c, d, f, h) {
    null == h && (h = f);
    f *= .5;
    h *= .5;
    f > c / 2 && (f = c / 2);
    h > d / 2 && (h = d / 2);
    c = a + c;
    d = b + d;
    var k = -f + .7071067811865476 * f,
        n = -f + .41421356237309503 * f,
        g = -h + .7071067811865476 * h,
        q = -h + .41421356237309503 * h;
    z.context.moveTo(c, d - h);
    z.context.quadraticCurveTo(c, d + q, c + k, d + g);
    z.context.quadraticCurveTo(c + n, d, c - f, d);
    z.context.lineTo(a + f, d);
    z.context.quadraticCurveTo(a - n, d, a - k, d + g);
    z.context.quadraticCurveTo(a, d + q, a, d - h);
    z.context.lineTo(a, b + h);
    z.context.quadraticCurveTo(a,
        b - q, a - k, b - g);
    z.context.quadraticCurveTo(a - n, b, a + f, b);
    z.context.lineTo(c - f, b);
    z.context.quadraticCurveTo(c + n, b, c + k, b - g);
    z.context.quadraticCurveTo(c, b - q, c, b + h);
    z.context.lineTo(c, d - h)
};
z.endFill = function() {
    z.context.beginPath();
    z.playCommands(z.fillCommands, !1);
    z.fillCommands.clear()
};
z.endStroke = function() {
    z.context.beginPath();
    z.playCommands(z.strokeCommands, !0);
    z.context.closePath();
    z.strokeCommands.clear()
};
z.hitTest = function(a, b, c) {
    z.bounds = a.__bounds;
    z.graphics = a;
    if (0 == a.__commands.get_length() ||
        null == z.bounds || 0 >= z.bounds.width || 0 >= z.bounds.height) return z.graphics = null, !1;
    z.hitTesting = !0;
    var d = a.__renderTransform,
        f = b * d.b + c * d.d + d.ty;
    b = b * d.a + c * d.c + d.tx - (z.bounds.x * d.a + z.bounds.y * d.c + d.tx);
    c = f - (z.bounds.x * d.b + z.bounds.y * d.d + d.ty);
    f = a.__canvas;
    var h = a.__context;
    a.__canvas = z.hitTestCanvas;
    a.__context = z.hitTestContext;
    z.context = a.__context;
    z.context.setTransform(d.a, d.b, d.c, d.d, d.tx, d.ty);
    z.fillCommands.clear();
    z.strokeCommands.clear();
    z.hasFill = !1;
    z.hasStroke = !1;
    z.bitmapFill = null;
    z.bitmapRepeat = !1;
    z.windingRule = "evenodd";
    d = new ue(a.__commands);
    for (var k = 0, n = a.__commands.types; k < n.length;) {
        var g = n[k];
        ++k;
        switch (g._hx_index) {
            case 0:
            case 1:
            case 2:
            case 3:
                z.endFill();
                if (z.hasFill && z.context.isPointInPath(b, c, z.windingRule)) return d.destroy(), a.__canvas = f, a.__context = h, z.graphics = null, !0;
                z.endStroke();
                if (z.hasStroke && z.context.isPointInStroke(b, c)) return d.destroy(), a.__canvas = f, a.__context = h, z.graphics = null, !0;
                if (g == da.BEGIN_BITMAP_FILL) {
                    switch (d.prev._hx_index) {
                        case 0:
                            d.oPos += 2;
                            d.bPos += 2;
                            break;
                        case 1:
                            d.iPos += 1;
                            d.fPos += 1;
                            break;
                        case 2:
                            d.oPos += 4;
                            d.iiPos += 2;
                            d.ffPos += 1;
                            d.fPos += 1;
                            break;
                        case 3:
                            d.oPos += 1;
                            break;
                        case 4:
                            d.fPos += 6;
                            break;
                        case 5:
                            d.fPos += 4;
                            break;
                        case 6:
                            d.fPos += 3;
                            break;
                        case 7:
                            d.fPos += 4;
                            break;
                        case 8:
                            d.oPos += 3;
                            break;
                        case 9:
                            d.fPos += 4;
                            break;
                        case 10:
                            d.fPos += 5;
                            d.oPos += 1;
                            break;
                        case 12:
                            d.oPos += 4;
                            break;
                        case 14:
                            d.oPos += 2;
                            d.bPos += 2;
                            break;
                        case 15:
                            d.oPos += 4;
                            d.iiPos += 2;
                            d.ffPos += 1;
                            d.fPos += 1;
                            break;
                        case 16:
                            d.oPos += 4;
                            d.iPos += 1;
                            d.fPos += 2;
                            d.bPos += 1;
                            break;
                        case 17:
                            d.fPos += 2;
                            break;
                        case 18:
                            d.fPos += 2;
                            break;
                        case 19:
                            d.oPos +=
                                1;
                            break;
                        case 20:
                            d.oPos += 1
                    }
                    d.prev = da.BEGIN_BITMAP_FILL;
                    g = d;
                    z.fillCommands.beginBitmapFill(g.buffer.o[g.oPos], g.buffer.o[g.oPos + 1], g.buffer.b[g.bPos], g.buffer.b[g.bPos + 1]);
                    z.strokeCommands.beginBitmapFill(g.buffer.o[g.oPos], g.buffer.o[g.oPos + 1], g.buffer.b[g.bPos], g.buffer.b[g.bPos + 1])
                } else if (g == da.BEGIN_GRADIENT_FILL) {
                    switch (d.prev._hx_index) {
                        case 0:
                            d.oPos += 2;
                            d.bPos += 2;
                            break;
                        case 1:
                            d.iPos += 1;
                            d.fPos += 1;
                            break;
                        case 2:
                            d.oPos += 4;
                            d.iiPos += 2;
                            d.ffPos += 1;
                            d.fPos += 1;
                            break;
                        case 3:
                            d.oPos += 1;
                            break;
                        case 4:
                            d.fPos +=
                                6;
                            break;
                        case 5:
                            d.fPos += 4;
                            break;
                        case 6:
                            d.fPos += 3;
                            break;
                        case 7:
                            d.fPos += 4;
                            break;
                        case 8:
                            d.oPos += 3;
                            break;
                        case 9:
                            d.fPos += 4;
                            break;
                        case 10:
                            d.fPos += 5;
                            d.oPos += 1;
                            break;
                        case 12:
                            d.oPos += 4;
                            break;
                        case 14:
                            d.oPos += 2;
                            d.bPos += 2;
                            break;
                        case 15:
                            d.oPos += 4;
                            d.iiPos += 2;
                            d.ffPos += 1;
                            d.fPos += 1;
                            break;
                        case 16:
                            d.oPos += 4;
                            d.iPos += 1;
                            d.fPos += 2;
                            d.bPos += 1;
                            break;
                        case 17:
                            d.fPos += 2;
                            break;
                        case 18:
                            d.fPos += 2;
                            break;
                        case 19:
                            d.oPos += 1;
                            break;
                        case 20:
                            d.oPos += 1
                    }
                    d.prev = da.BEGIN_GRADIENT_FILL;
                    g = d;
                    z.fillCommands.beginGradientFill(g.buffer.o[g.oPos],
                        g.buffer.ii[g.iiPos], g.buffer.ff[g.ffPos], g.buffer.ii[g.iiPos + 1], g.buffer.o[g.oPos + 1], g.buffer.o[g.oPos + 2], g.buffer.o[g.oPos + 3], g.buffer.f[g.fPos]);
                    z.strokeCommands.beginGradientFill(g.buffer.o[g.oPos], g.buffer.ii[g.iiPos], g.buffer.ff[g.ffPos], g.buffer.ii[g.iiPos + 1], g.buffer.o[g.oPos + 1], g.buffer.o[g.oPos + 2], g.buffer.o[g.oPos + 3], g.buffer.f[g.fPos])
                } else if (g == da.BEGIN_SHADER_FILL) {
                    switch (d.prev._hx_index) {
                        case 0:
                            d.oPos += 2;
                            d.bPos += 2;
                            break;
                        case 1:
                            d.iPos += 1;
                            d.fPos += 1;
                            break;
                        case 2:
                            d.oPos += 4;
                            d.iiPos += 2;
                            d.ffPos += 1;
                            d.fPos += 1;
                            break;
                        case 3:
                            d.oPos += 1;
                            break;
                        case 4:
                            d.fPos += 6;
                            break;
                        case 5:
                            d.fPos += 4;
                            break;
                        case 6:
                            d.fPos += 3;
                            break;
                        case 7:
                            d.fPos += 4;
                            break;
                        case 8:
                            d.oPos += 3;
                            break;
                        case 9:
                            d.fPos += 4;
                            break;
                        case 10:
                            d.fPos += 5;
                            d.oPos += 1;
                            break;
                        case 12:
                            d.oPos += 4;
                            break;
                        case 14:
                            d.oPos += 2;
                            d.bPos += 2;
                            break;
                        case 15:
                            d.oPos += 4;
                            d.iiPos += 2;
                            d.ffPos += 1;
                            d.fPos += 1;
                            break;
                        case 16:
                            d.oPos += 4;
                            d.iPos += 1;
                            d.fPos += 2;
                            d.bPos += 1;
                            break;
                        case 17:
                            d.fPos += 2;
                            break;
                        case 18:
                            d.fPos += 2;
                            break;
                        case 19:
                            d.oPos += 1;
                            break;
                        case 20:
                            d.oPos += 1
                    }
                    d.prev = da.BEGIN_SHADER_FILL;
                    g = d;
                    z.fillCommands.beginShaderFill(g.buffer.o[g.oPos]);
                    z.strokeCommands.beginShaderFill(g.buffer.o[g.oPos])
                } else {
                    switch (d.prev._hx_index) {
                        case 0:
                            d.oPos += 2;
                            d.bPos += 2;
                            break;
                        case 1:
                            d.iPos += 1;
                            d.fPos += 1;
                            break;
                        case 2:
                            d.oPos += 4;
                            d.iiPos += 2;
                            d.ffPos += 1;
                            d.fPos += 1;
                            break;
                        case 3:
                            d.oPos += 1;
                            break;
                        case 4:
                            d.fPos += 6;
                            break;
                        case 5:
                            d.fPos += 4;
                            break;
                        case 6:
                            d.fPos += 3;
                            break;
                        case 7:
                            d.fPos += 4;
                            break;
                        case 8:
                            d.oPos += 3;
                            break;
                        case 9:
                            d.fPos += 4;
                            break;
                        case 10:
                            d.fPos += 5;
                            d.oPos += 1;
                            break;
                        case 12:
                            d.oPos += 4;
                            break;
                        case 14:
                            d.oPos += 2;
                            d.bPos +=
                                2;
                            break;
                        case 15:
                            d.oPos += 4;
                            d.iiPos += 2;
                            d.ffPos += 1;
                            d.fPos += 1;
                            break;
                        case 16:
                            d.oPos += 4;
                            d.iPos += 1;
                            d.fPos += 2;
                            d.bPos += 1;
                            break;
                        case 17:
                            d.fPos += 2;
                            break;
                        case 18:
                            d.fPos += 2;
                            break;
                        case 19:
                            d.oPos += 1;
                            break;
                        case 20:
                            d.oPos += 1
                    }
                    d.prev = da.BEGIN_FILL;
                    g = d;
                    z.fillCommands.beginFill(g.buffer.i[g.iPos], 1);
                    z.strokeCommands.beginFill(g.buffer.i[g.iPos], 1)
                }
                break;
            case 4:
                switch (d.prev._hx_index) {
                    case 0:
                        d.oPos += 2;
                        d.bPos += 2;
                        break;
                    case 1:
                        d.iPos += 1;
                        d.fPos += 1;
                        break;
                    case 2:
                        d.oPos += 4;
                        d.iiPos += 2;
                        d.ffPos += 1;
                        d.fPos += 1;
                        break;
                    case 3:
                        d.oPos +=
                            1;
                        break;
                    case 4:
                        d.fPos += 6;
                        break;
                    case 5:
                        d.fPos += 4;
                        break;
                    case 6:
                        d.fPos += 3;
                        break;
                    case 7:
                        d.fPos += 4;
                        break;
                    case 8:
                        d.oPos += 3;
                        break;
                    case 9:
                        d.fPos += 4;
                        break;
                    case 10:
                        d.fPos += 5;
                        d.oPos += 1;
                        break;
                    case 12:
                        d.oPos += 4;
                        break;
                    case 14:
                        d.oPos += 2;
                        d.bPos += 2;
                        break;
                    case 15:
                        d.oPos += 4;
                        d.iiPos += 2;
                        d.ffPos += 1;
                        d.fPos += 1;
                        break;
                    case 16:
                        d.oPos += 4;
                        d.iPos += 1;
                        d.fPos += 2;
                        d.bPos += 1;
                        break;
                    case 17:
                        d.fPos += 2;
                        break;
                    case 18:
                        d.fPos += 2;
                        break;
                    case 19:
                        d.oPos += 1;
                        break;
                    case 20:
                        d.oPos += 1
                }
                d.prev = da.CUBIC_CURVE_TO;
                g = d;
                z.fillCommands.cubicCurveTo(g.buffer.f[g.fPos],
                    g.buffer.f[g.fPos + 1], g.buffer.f[g.fPos + 2], g.buffer.f[g.fPos + 3], g.buffer.f[g.fPos + 4], g.buffer.f[g.fPos + 5]);
                z.strokeCommands.cubicCurveTo(g.buffer.f[g.fPos], g.buffer.f[g.fPos + 1], g.buffer.f[g.fPos + 2], g.buffer.f[g.fPos + 3], g.buffer.f[g.fPos + 4], g.buffer.f[g.fPos + 5]);
                break;
            case 5:
                switch (d.prev._hx_index) {
                    case 0:
                        d.oPos += 2;
                        d.bPos += 2;
                        break;
                    case 1:
                        d.iPos += 1;
                        d.fPos += 1;
                        break;
                    case 2:
                        d.oPos += 4;
                        d.iiPos += 2;
                        d.ffPos += 1;
                        d.fPos += 1;
                        break;
                    case 3:
                        d.oPos += 1;
                        break;
                    case 4:
                        d.fPos += 6;
                        break;
                    case 5:
                        d.fPos += 4;
                        break;
                    case 6:
                        d.fPos +=
                            3;
                        break;
                    case 7:
                        d.fPos += 4;
                        break;
                    case 8:
                        d.oPos += 3;
                        break;
                    case 9:
                        d.fPos += 4;
                        break;
                    case 10:
                        d.fPos += 5;
                        d.oPos += 1;
                        break;
                    case 12:
                        d.oPos += 4;
                        break;
                    case 14:
                        d.oPos += 2;
                        d.bPos += 2;
                        break;
                    case 15:
                        d.oPos += 4;
                        d.iiPos += 2;
                        d.ffPos += 1;
                        d.fPos += 1;
                        break;
                    case 16:
                        d.oPos += 4;
                        d.iPos += 1;
                        d.fPos += 2;
                        d.bPos += 1;
                        break;
                    case 17:
                        d.fPos += 2;
                        break;
                    case 18:
                        d.fPos += 2;
                        break;
                    case 19:
                        d.oPos += 1;
                        break;
                    case 20:
                        d.oPos += 1
                }
                d.prev = da.CURVE_TO;
                g = d;
                z.fillCommands.curveTo(g.buffer.f[g.fPos], g.buffer.f[g.fPos + 1], g.buffer.f[g.fPos + 2], g.buffer.f[g.fPos + 3]);
                z.strokeCommands.curveTo(g.buffer.f[g.fPos],
                    g.buffer.f[g.fPos + 1], g.buffer.f[g.fPos + 2], g.buffer.f[g.fPos + 3]);
                break;
            case 6:
                switch (d.prev._hx_index) {
                    case 0:
                        d.oPos += 2;
                        d.bPos += 2;
                        break;
                    case 1:
                        d.iPos += 1;
                        d.fPos += 1;
                        break;
                    case 2:
                        d.oPos += 4;
                        d.iiPos += 2;
                        d.ffPos += 1;
                        d.fPos += 1;
                        break;
                    case 3:
                        d.oPos += 1;
                        break;
                    case 4:
                        d.fPos += 6;
                        break;
                    case 5:
                        d.fPos += 4;
                        break;
                    case 6:
                        d.fPos += 3;
                        break;
                    case 7:
                        d.fPos += 4;
                        break;
                    case 8:
                        d.oPos += 3;
                        break;
                    case 9:
                        d.fPos += 4;
                        break;
                    case 10:
                        d.fPos += 5;
                        d.oPos += 1;
                        break;
                    case 12:
                        d.oPos += 4;
                        break;
                    case 14:
                        d.oPos += 2;
                        d.bPos += 2;
                        break;
                    case 15:
                        d.oPos += 4;
                        d.iiPos +=
                            2;
                        d.ffPos += 1;
                        d.fPos += 1;
                        break;
                    case 16:
                        d.oPos += 4;
                        d.iPos += 1;
                        d.fPos += 2;
                        d.bPos += 1;
                        break;
                    case 17:
                        d.fPos += 2;
                        break;
                    case 18:
                        d.fPos += 2;
                        break;
                    case 19:
                        d.oPos += 1;
                        break;
                    case 20:
                        d.oPos += 1
                }
                d.prev = da.DRAW_CIRCLE;
                g = d;
                z.fillCommands.drawCircle(g.buffer.f[g.fPos], g.buffer.f[g.fPos + 1], g.buffer.f[g.fPos + 2]);
                z.strokeCommands.drawCircle(g.buffer.f[g.fPos], g.buffer.f[g.fPos + 1], g.buffer.f[g.fPos + 2]);
                break;
            case 7:
                switch (d.prev._hx_index) {
                    case 0:
                        d.oPos += 2;
                        d.bPos += 2;
                        break;
                    case 1:
                        d.iPos += 1;
                        d.fPos += 1;
                        break;
                    case 2:
                        d.oPos += 4;
                        d.iiPos +=
                            2;
                        d.ffPos += 1;
                        d.fPos += 1;
                        break;
                    case 3:
                        d.oPos += 1;
                        break;
                    case 4:
                        d.fPos += 6;
                        break;
                    case 5:
                        d.fPos += 4;
                        break;
                    case 6:
                        d.fPos += 3;
                        break;
                    case 7:
                        d.fPos += 4;
                        break;
                    case 8:
                        d.oPos += 3;
                        break;
                    case 9:
                        d.fPos += 4;
                        break;
                    case 10:
                        d.fPos += 5;
                        d.oPos += 1;
                        break;
                    case 12:
                        d.oPos += 4;
                        break;
                    case 14:
                        d.oPos += 2;
                        d.bPos += 2;
                        break;
                    case 15:
                        d.oPos += 4;
                        d.iiPos += 2;
                        d.ffPos += 1;
                        d.fPos += 1;
                        break;
                    case 16:
                        d.oPos += 4;
                        d.iPos += 1;
                        d.fPos += 2;
                        d.bPos += 1;
                        break;
                    case 17:
                        d.fPos += 2;
                        break;
                    case 18:
                        d.fPos += 2;
                        break;
                    case 19:
                        d.oPos += 1;
                        break;
                    case 20:
                        d.oPos += 1
                }
                d.prev = da.DRAW_ELLIPSE;
                g = d;
                z.fillCommands.drawEllipse(g.buffer.f[g.fPos], g.buffer.f[g.fPos + 1], g.buffer.f[g.fPos + 2], g.buffer.f[g.fPos + 3]);
                z.strokeCommands.drawEllipse(g.buffer.f[g.fPos], g.buffer.f[g.fPos + 1], g.buffer.f[g.fPos + 2], g.buffer.f[g.fPos + 3]);
                break;
            case 9:
                switch (d.prev._hx_index) {
                    case 0:
                        d.oPos += 2;
                        d.bPos += 2;
                        break;
                    case 1:
                        d.iPos += 1;
                        d.fPos += 1;
                        break;
                    case 2:
                        d.oPos += 4;
                        d.iiPos += 2;
                        d.ffPos += 1;
                        d.fPos += 1;
                        break;
                    case 3:
                        d.oPos += 1;
                        break;
                    case 4:
                        d.fPos += 6;
                        break;
                    case 5:
                        d.fPos += 4;
                        break;
                    case 6:
                        d.fPos += 3;
                        break;
                    case 7:
                        d.fPos += 4;
                        break;
                    case 8:
                        d.oPos +=
                            3;
                        break;
                    case 9:
                        d.fPos += 4;
                        break;
                    case 10:
                        d.fPos += 5;
                        d.oPos += 1;
                        break;
                    case 12:
                        d.oPos += 4;
                        break;
                    case 14:
                        d.oPos += 2;
                        d.bPos += 2;
                        break;
                    case 15:
                        d.oPos += 4;
                        d.iiPos += 2;
                        d.ffPos += 1;
                        d.fPos += 1;
                        break;
                    case 16:
                        d.oPos += 4;
                        d.iPos += 1;
                        d.fPos += 2;
                        d.bPos += 1;
                        break;
                    case 17:
                        d.fPos += 2;
                        break;
                    case 18:
                        d.fPos += 2;
                        break;
                    case 19:
                        d.oPos += 1;
                        break;
                    case 20:
                        d.oPos += 1
                }
                d.prev = da.DRAW_RECT;
                g = d;
                z.fillCommands.drawRect(g.buffer.f[g.fPos], g.buffer.f[g.fPos + 1], g.buffer.f[g.fPos + 2], g.buffer.f[g.fPos + 3]);
                z.strokeCommands.drawRect(g.buffer.f[g.fPos], g.buffer.f[g.fPos +
                    1], g.buffer.f[g.fPos + 2], g.buffer.f[g.fPos + 3]);
                break;
            case 10:
                switch (d.prev._hx_index) {
                    case 0:
                        d.oPos += 2;
                        d.bPos += 2;
                        break;
                    case 1:
                        d.iPos += 1;
                        d.fPos += 1;
                        break;
                    case 2:
                        d.oPos += 4;
                        d.iiPos += 2;
                        d.ffPos += 1;
                        d.fPos += 1;
                        break;
                    case 3:
                        d.oPos += 1;
                        break;
                    case 4:
                        d.fPos += 6;
                        break;
                    case 5:
                        d.fPos += 4;
                        break;
                    case 6:
                        d.fPos += 3;
                        break;
                    case 7:
                        d.fPos += 4;
                        break;
                    case 8:
                        d.oPos += 3;
                        break;
                    case 9:
                        d.fPos += 4;
                        break;
                    case 10:
                        d.fPos += 5;
                        d.oPos += 1;
                        break;
                    case 12:
                        d.oPos += 4;
                        break;
                    case 14:
                        d.oPos += 2;
                        d.bPos += 2;
                        break;
                    case 15:
                        d.oPos += 4;
                        d.iiPos += 2;
                        d.ffPos += 1;
                        d.fPos +=
                            1;
                        break;
                    case 16:
                        d.oPos += 4;
                        d.iPos += 1;
                        d.fPos += 2;
                        d.bPos += 1;
                        break;
                    case 17:
                        d.fPos += 2;
                        break;
                    case 18:
                        d.fPos += 2;
                        break;
                    case 19:
                        d.oPos += 1;
                        break;
                    case 20:
                        d.oPos += 1
                }
                d.prev = da.DRAW_ROUND_RECT;
                g = d;
                z.fillCommands.drawRoundRect(g.buffer.f[g.fPos], g.buffer.f[g.fPos + 1], g.buffer.f[g.fPos + 2], g.buffer.f[g.fPos + 3], g.buffer.f[g.fPos + 4], g.buffer.o[g.oPos]);
                z.strokeCommands.drawRoundRect(g.buffer.f[g.fPos], g.buffer.f[g.fPos + 1], g.buffer.f[g.fPos + 2], g.buffer.f[g.fPos + 3], g.buffer.f[g.fPos + 4], g.buffer.o[g.oPos]);
                break;
            case 13:
                switch (d.prev._hx_index) {
                    case 0:
                        d.oPos +=
                            2;
                        d.bPos += 2;
                        break;
                    case 1:
                        d.iPos += 1;
                        d.fPos += 1;
                        break;
                    case 2:
                        d.oPos += 4;
                        d.iiPos += 2;
                        d.ffPos += 1;
                        d.fPos += 1;
                        break;
                    case 3:
                        d.oPos += 1;
                        break;
                    case 4:
                        d.fPos += 6;
                        break;
                    case 5:
                        d.fPos += 4;
                        break;
                    case 6:
                        d.fPos += 3;
                        break;
                    case 7:
                        d.fPos += 4;
                        break;
                    case 8:
                        d.oPos += 3;
                        break;
                    case 9:
                        d.fPos += 4;
                        break;
                    case 10:
                        d.fPos += 5;
                        d.oPos += 1;
                        break;
                    case 12:
                        d.oPos += 4;
                        break;
                    case 14:
                        d.oPos += 2;
                        d.bPos += 2;
                        break;
                    case 15:
                        d.oPos += 4;
                        d.iiPos += 2;
                        d.ffPos += 1;
                        d.fPos += 1;
                        break;
                    case 16:
                        d.oPos += 4;
                        d.iPos += 1;
                        d.fPos += 2;
                        d.bPos += 1;
                        break;
                    case 17:
                        d.fPos += 2;
                        break;
                    case 18:
                        d.fPos +=
                            2;
                        break;
                    case 19:
                        d.oPos += 1;
                        break;
                    case 20:
                        d.oPos += 1
                }
                d.prev = da.END_FILL;
                z.endFill();
                if (z.hasFill && z.context.isPointInPath(b, c, z.windingRule)) return d.destroy(), a.__canvas = f, a.__context = h, z.graphics = null, !0;
                z.endStroke();
                if (z.hasStroke && z.context.isPointInStroke(b, c)) return d.destroy(), a.__canvas = f, a.__context = h, z.graphics = null, !0;
                z.hasFill = !1;
                z.bitmapFill = null;
                break;
            case 14:
                switch (d.prev._hx_index) {
                    case 0:
                        d.oPos += 2;
                        d.bPos += 2;
                        break;
                    case 1:
                        d.iPos += 1;
                        d.fPos += 1;
                        break;
                    case 2:
                        d.oPos += 4;
                        d.iiPos += 2;
                        d.ffPos +=
                            1;
                        d.fPos += 1;
                        break;
                    case 3:
                        d.oPos += 1;
                        break;
                    case 4:
                        d.fPos += 6;
                        break;
                    case 5:
                        d.fPos += 4;
                        break;
                    case 6:
                        d.fPos += 3;
                        break;
                    case 7:
                        d.fPos += 4;
                        break;
                    case 8:
                        d.oPos += 3;
                        break;
                    case 9:
                        d.fPos += 4;
                        break;
                    case 10:
                        d.fPos += 5;
                        d.oPos += 1;
                        break;
                    case 12:
                        d.oPos += 4;
                        break;
                    case 14:
                        d.oPos += 2;
                        d.bPos += 2;
                        break;
                    case 15:
                        d.oPos += 4;
                        d.iiPos += 2;
                        d.ffPos += 1;
                        d.fPos += 1;
                        break;
                    case 16:
                        d.oPos += 4;
                        d.iPos += 1;
                        d.fPos += 2;
                        d.bPos += 1;
                        break;
                    case 17:
                        d.fPos += 2;
                        break;
                    case 18:
                        d.fPos += 2;
                        break;
                    case 19:
                        d.oPos += 1;
                        break;
                    case 20:
                        d.oPos += 1
                }
                d.prev = da.LINE_BITMAP_STYLE;
                g =
                    d;
                z.strokeCommands.lineBitmapStyle(g.buffer.o[g.oPos], g.buffer.o[g.oPos + 1], g.buffer.b[g.bPos], g.buffer.b[g.bPos + 1]);
                break;
            case 15:
                switch (d.prev._hx_index) {
                    case 0:
                        d.oPos += 2;
                        d.bPos += 2;
                        break;
                    case 1:
                        d.iPos += 1;
                        d.fPos += 1;
                        break;
                    case 2:
                        d.oPos += 4;
                        d.iiPos += 2;
                        d.ffPos += 1;
                        d.fPos += 1;
                        break;
                    case 3:
                        d.oPos += 1;
                        break;
                    case 4:
                        d.fPos += 6;
                        break;
                    case 5:
                        d.fPos += 4;
                        break;
                    case 6:
                        d.fPos += 3;
                        break;
                    case 7:
                        d.fPos += 4;
                        break;
                    case 8:
                        d.oPos += 3;
                        break;
                    case 9:
                        d.fPos += 4;
                        break;
                    case 10:
                        d.fPos += 5;
                        d.oPos += 1;
                        break;
                    case 12:
                        d.oPos += 4;
                        break;
                    case 14:
                        d.oPos +=
                            2;
                        d.bPos += 2;
                        break;
                    case 15:
                        d.oPos += 4;
                        d.iiPos += 2;
                        d.ffPos += 1;
                        d.fPos += 1;
                        break;
                    case 16:
                        d.oPos += 4;
                        d.iPos += 1;
                        d.fPos += 2;
                        d.bPos += 1;
                        break;
                    case 17:
                        d.fPos += 2;
                        break;
                    case 18:
                        d.fPos += 2;
                        break;
                    case 19:
                        d.oPos += 1;
                        break;
                    case 20:
                        d.oPos += 1
                }
                d.prev = da.LINE_GRADIENT_STYLE;
                g = d;
                z.strokeCommands.lineGradientStyle(g.buffer.o[g.oPos], g.buffer.ii[g.iiPos], g.buffer.ff[g.ffPos], g.buffer.ii[g.iiPos + 1], g.buffer.o[g.oPos + 1], g.buffer.o[g.oPos + 2], g.buffer.o[g.oPos + 3], g.buffer.f[g.fPos]);
                break;
            case 16:
                z.endStroke();
                if (z.hasStroke && z.context.isPointInStroke(b,
                        c)) return d.destroy(), a.__canvas = f, a.__context = h, z.graphics = null, !0;
                switch (d.prev._hx_index) {
                    case 0:
                        d.oPos += 2;
                        d.bPos += 2;
                        break;
                    case 1:
                        d.iPos += 1;
                        d.fPos += 1;
                        break;
                    case 2:
                        d.oPos += 4;
                        d.iiPos += 2;
                        d.ffPos += 1;
                        d.fPos += 1;
                        break;
                    case 3:
                        d.oPos += 1;
                        break;
                    case 4:
                        d.fPos += 6;
                        break;
                    case 5:
                        d.fPos += 4;
                        break;
                    case 6:
                        d.fPos += 3;
                        break;
                    case 7:
                        d.fPos += 4;
                        break;
                    case 8:
                        d.oPos += 3;
                        break;
                    case 9:
                        d.fPos += 4;
                        break;
                    case 10:
                        d.fPos += 5;
                        d.oPos += 1;
                        break;
                    case 12:
                        d.oPos += 4;
                        break;
                    case 14:
                        d.oPos += 2;
                        d.bPos += 2;
                        break;
                    case 15:
                        d.oPos += 4;
                        d.iiPos += 2;
                        d.ffPos +=
                            1;
                        d.fPos += 1;
                        break;
                    case 16:
                        d.oPos += 4;
                        d.iPos += 1;
                        d.fPos += 2;
                        d.bPos += 1;
                        break;
                    case 17:
                        d.fPos += 2;
                        break;
                    case 18:
                        d.fPos += 2;
                        break;
                    case 19:
                        d.oPos += 1;
                        break;
                    case 20:
                        d.oPos += 1
                }
                d.prev = da.LINE_STYLE;
                g = d;
                z.strokeCommands.lineStyle(g.buffer.o[g.oPos], g.buffer.i[g.iPos], 1, g.buffer.b[g.bPos], g.buffer.o[g.oPos + 1], g.buffer.o[g.oPos + 2], g.buffer.o[g.oPos + 3], g.buffer.f[g.fPos + 1]);
                break;
            case 17:
                switch (d.prev._hx_index) {
                    case 0:
                        d.oPos += 2;
                        d.bPos += 2;
                        break;
                    case 1:
                        d.iPos += 1;
                        d.fPos += 1;
                        break;
                    case 2:
                        d.oPos += 4;
                        d.iiPos += 2;
                        d.ffPos += 1;
                        d.fPos += 1;
                        break;
                    case 3:
                        d.oPos += 1;
                        break;
                    case 4:
                        d.fPos += 6;
                        break;
                    case 5:
                        d.fPos += 4;
                        break;
                    case 6:
                        d.fPos += 3;
                        break;
                    case 7:
                        d.fPos += 4;
                        break;
                    case 8:
                        d.oPos += 3;
                        break;
                    case 9:
                        d.fPos += 4;
                        break;
                    case 10:
                        d.fPos += 5;
                        d.oPos += 1;
                        break;
                    case 12:
                        d.oPos += 4;
                        break;
                    case 14:
                        d.oPos += 2;
                        d.bPos += 2;
                        break;
                    case 15:
                        d.oPos += 4;
                        d.iiPos += 2;
                        d.ffPos += 1;
                        d.fPos += 1;
                        break;
                    case 16:
                        d.oPos += 4;
                        d.iPos += 1;
                        d.fPos += 2;
                        d.bPos += 1;
                        break;
                    case 17:
                        d.fPos += 2;
                        break;
                    case 18:
                        d.fPos += 2;
                        break;
                    case 19:
                        d.oPos += 1;
                        break;
                    case 20:
                        d.oPos += 1
                }
                d.prev = da.LINE_TO;
                g = d;
                z.fillCommands.lineTo(g.buffer.f[g.fPos],
                    g.buffer.f[g.fPos + 1]);
                z.strokeCommands.lineTo(g.buffer.f[g.fPos], g.buffer.f[g.fPos + 1]);
                break;
            case 18:
                switch (d.prev._hx_index) {
                    case 0:
                        d.oPos += 2;
                        d.bPos += 2;
                        break;
                    case 1:
                        d.iPos += 1;
                        d.fPos += 1;
                        break;
                    case 2:
                        d.oPos += 4;
                        d.iiPos += 2;
                        d.ffPos += 1;
                        d.fPos += 1;
                        break;
                    case 3:
                        d.oPos += 1;
                        break;
                    case 4:
                        d.fPos += 6;
                        break;
                    case 5:
                        d.fPos += 4;
                        break;
                    case 6:
                        d.fPos += 3;
                        break;
                    case 7:
                        d.fPos += 4;
                        break;
                    case 8:
                        d.oPos += 3;
                        break;
                    case 9:
                        d.fPos += 4;
                        break;
                    case 10:
                        d.fPos += 5;
                        d.oPos += 1;
                        break;
                    case 12:
                        d.oPos += 4;
                        break;
                    case 14:
                        d.oPos += 2;
                        d.bPos += 2;
                        break;
                    case 15:
                        d.oPos +=
                            4;
                        d.iiPos += 2;
                        d.ffPos += 1;
                        d.fPos += 1;
                        break;
                    case 16:
                        d.oPos += 4;
                        d.iPos += 1;
                        d.fPos += 2;
                        d.bPos += 1;
                        break;
                    case 17:
                        d.fPos += 2;
                        break;
                    case 18:
                        d.fPos += 2;
                        break;
                    case 19:
                        d.oPos += 1;
                        break;
                    case 20:
                        d.oPos += 1
                }
                d.prev = da.MOVE_TO;
                g = d;
                z.fillCommands.moveTo(g.buffer.f[g.fPos], g.buffer.f[g.fPos + 1]);
                z.strokeCommands.moveTo(g.buffer.f[g.fPos], g.buffer.f[g.fPos + 1]);
                break;
            case 21:
                z.windingRule = "evenodd";
                break;
            case 22:
                z.windingRule = "nonzero";
                break;
            default:
                switch (d.prev._hx_index) {
                    case 0:
                        d.oPos += 2;
                        d.bPos += 2;
                        break;
                    case 1:
                        d.iPos += 1;
                        d.fPos +=
                            1;
                        break;
                    case 2:
                        d.oPos += 4;
                        d.iiPos += 2;
                        d.ffPos += 1;
                        d.fPos += 1;
                        break;
                    case 3:
                        d.oPos += 1;
                        break;
                    case 4:
                        d.fPos += 6;
                        break;
                    case 5:
                        d.fPos += 4;
                        break;
                    case 6:
                        d.fPos += 3;
                        break;
                    case 7:
                        d.fPos += 4;
                        break;
                    case 8:
                        d.oPos += 3;
                        break;
                    case 9:
                        d.fPos += 4;
                        break;
                    case 10:
                        d.fPos += 5;
                        d.oPos += 1;
                        break;
                    case 12:
                        d.oPos += 4;
                        break;
                    case 14:
                        d.oPos += 2;
                        d.bPos += 2;
                        break;
                    case 15:
                        d.oPos += 4;
                        d.iiPos += 2;
                        d.ffPos += 1;
                        d.fPos += 1;
                        break;
                    case 16:
                        d.oPos += 4;
                        d.iPos += 1;
                        d.fPos += 2;
                        d.bPos += 1;
                        break;
                    case 17:
                        d.fPos += 2;
                        break;
                    case 18:
                        d.fPos += 2;
                        break;
                    case 19:
                        d.oPos += 1;
                        break;
                    case 20:
                        d.oPos +=
                            1
                }
                d.prev = g
        }
    }
    k = !1;
    0 < z.fillCommands.get_length() && z.endFill();
    z.hasFill && z.context.isPointInPath(b, c, z.windingRule) && (k = !0);
    0 < z.strokeCommands.get_length() && z.endStroke();
    z.hasStroke && z.context.isPointInStroke(b, c) && (k = !0);
    d.destroy();
    a.__canvas = f;
    a.__context = h;
    z.graphics = null;
    return k
};
z.normalizeUVT = function(a, b) {
    null == b && (b = !1);
    for (var c = -Infinity, d, f = a.get_length(), h = 1, k = f + 1; h < k;) d = h++, b && 0 == d % 3 || (d = a.get(d - 1), c < d && (c = d));
    if (!b) return {
        max: c,
        uvt: a
    };
    var g = la.toFloatVector(null);
    h = 1;
    for (k = f + 1; h < k;) d =
        h++, b && 0 == d % 3 || g.push(a.get(d - 1));
    return {
        max: c,
        uvt: g
    }
};
z.playCommands = function(a, b) {
    null == b && (b = !1);
    z.bounds = z.graphics.__bounds;
    var c = z.bounds.x,
        d = z.bounds.y,
        f = 0,
        h = 0,
        k = !1,
        g = 0,
        p = 0,
        q = !1;
    z.windingRule = "evenodd";
    z.setSmoothing(!0);
    var u = !1,
        m = new ue(a),
        r, l, D, x = null,
        w = null,
        J = 0,
        y = a.types;
    a: for (; J < y.length;) {
        var C = y[J];
        ++J;
        switch (C._hx_index) {
            case 0:
                switch (m.prev._hx_index) {
                    case 0:
                        m.oPos += 2;
                        m.bPos += 2;
                        break;
                    case 1:
                        m.iPos += 1;
                        m.fPos += 1;
                        break;
                    case 2:
                        m.oPos += 4;
                        m.iiPos += 2;
                        m.ffPos += 1;
                        m.fPos += 1;
                        break;
                    case 3:
                        m.oPos +=
                            1;
                        break;
                    case 4:
                        m.fPos += 6;
                        break;
                    case 5:
                        m.fPos += 4;
                        break;
                    case 6:
                        m.fPos += 3;
                        break;
                    case 7:
                        m.fPos += 4;
                        break;
                    case 8:
                        m.oPos += 3;
                        break;
                    case 9:
                        m.fPos += 4;
                        break;
                    case 10:
                        m.fPos += 5;
                        m.oPos += 1;
                        break;
                    case 12:
                        m.oPos += 4;
                        break;
                    case 14:
                        m.oPos += 2;
                        m.bPos += 2;
                        break;
                    case 15:
                        m.oPos += 4;
                        m.iiPos += 2;
                        m.ffPos += 1;
                        m.fPos += 1;
                        break;
                    case 16:
                        m.oPos += 4;
                        m.iPos += 1;
                        m.fPos += 2;
                        m.bPos += 1;
                        break;
                    case 17:
                        m.fPos += 2;
                        break;
                    case 18:
                        m.fPos += 2;
                        break;
                    case 19:
                        m.oPos += 1;
                        break;
                    case 20:
                        m.oPos += 1
                }
                m.prev = da.BEGIN_BITMAP_FILL;
                var t = m;
                z.bitmapFill = t.buffer.o[t.oPos];
                if (t.buffer.o[t.oPos].readable) z.context.fillStyle = z.createBitmapFill(t.buffer.o[t.oPos], t.buffer.b[t.bPos], t.buffer.b[t.bPos + 1]);
                else {
                    var v = O.hex(0, 6);
                    z.context.fillStyle = "#" + v
                }
                z.hasFill = !0;
                null != t.buffer.o[t.oPos + 1] ? (z.pendingMatrix = t.buffer.o[t.oPos + 1], z.inversePendingMatrix = t.buffer.o[t.oPos + 1].clone(), z.inversePendingMatrix.invert()) : (z.pendingMatrix = null, z.inversePendingMatrix = null);
                break;
            case 1:
                switch (m.prev._hx_index) {
                    case 0:
                        m.oPos += 2;
                        m.bPos += 2;
                        break;
                    case 1:
                        m.iPos += 1;
                        m.fPos += 1;
                        break;
                    case 2:
                        m.oPos +=
                            4;
                        m.iiPos += 2;
                        m.ffPos += 1;
                        m.fPos += 1;
                        break;
                    case 3:
                        m.oPos += 1;
                        break;
                    case 4:
                        m.fPos += 6;
                        break;
                    case 5:
                        m.fPos += 4;
                        break;
                    case 6:
                        m.fPos += 3;
                        break;
                    case 7:
                        m.fPos += 4;
                        break;
                    case 8:
                        m.oPos += 3;
                        break;
                    case 9:
                        m.fPos += 4;
                        break;
                    case 10:
                        m.fPos += 5;
                        m.oPos += 1;
                        break;
                    case 12:
                        m.oPos += 4;
                        break;
                    case 14:
                        m.oPos += 2;
                        m.bPos += 2;
                        break;
                    case 15:
                        m.oPos += 4;
                        m.iiPos += 2;
                        m.ffPos += 1;
                        m.fPos += 1;
                        break;
                    case 16:
                        m.oPos += 4;
                        m.iPos += 1;
                        m.fPos += 2;
                        m.bPos += 1;
                        break;
                    case 17:
                        m.fPos += 2;
                        break;
                    case 18:
                        m.fPos += 2;
                        break;
                    case 19:
                        m.oPos += 1;
                        break;
                    case 20:
                        m.oPos += 1
                }
                m.prev = da.BEGIN_FILL;
                var G = m;
                if (.005 > G.buffer.f[G.fPos]) z.hasFill = !1;
                else {
                    if (1 == G.buffer.f[G.fPos]) {
                        var F = O.hex(G.buffer.i[G.iPos] & 16777215, 6);
                        z.context.fillStyle = "#" + F
                    } else {
                        var eb = (G.buffer.i[G.iPos] & 16711680) >>> 16;
                        var H = (G.buffer.i[G.iPos] & 65280) >>> 8;
                        var lb = G.buffer.i[G.iPos] & 255;
                        z.context.fillStyle = "rgba(" + eb + ", " + H + ", " + lb + ", " + G.buffer.f[G.fPos] + ")"
                    }
                    z.bitmapFill = null;
                    z.setSmoothing(!0);
                    z.hasFill = !0
                }
                break;
            case 2:
                switch (m.prev._hx_index) {
                    case 0:
                        m.oPos += 2;
                        m.bPos += 2;
                        break;
                    case 1:
                        m.iPos += 1;
                        m.fPos += 1;
                        break;
                    case 2:
                        m.oPos +=
                            4;
                        m.iiPos += 2;
                        m.ffPos += 1;
                        m.fPos += 1;
                        break;
                    case 3:
                        m.oPos += 1;
                        break;
                    case 4:
                        m.fPos += 6;
                        break;
                    case 5:
                        m.fPos += 4;
                        break;
                    case 6:
                        m.fPos += 3;
                        break;
                    case 7:
                        m.fPos += 4;
                        break;
                    case 8:
                        m.oPos += 3;
                        break;
                    case 9:
                        m.fPos += 4;
                        break;
                    case 10:
                        m.fPos += 5;
                        m.oPos += 1;
                        break;
                    case 12:
                        m.oPos += 4;
                        break;
                    case 14:
                        m.oPos += 2;
                        m.bPos += 2;
                        break;
                    case 15:
                        m.oPos += 4;
                        m.iiPos += 2;
                        m.ffPos += 1;
                        m.fPos += 1;
                        break;
                    case 16:
                        m.oPos += 4;
                        m.iPos += 1;
                        m.fPos += 2;
                        m.bPos += 1;
                        break;
                    case 17:
                        m.fPos += 2;
                        break;
                    case 18:
                        m.fPos += 2;
                        break;
                    case 19:
                        m.oPos += 1;
                        break;
                    case 20:
                        m.oPos += 1
                }
                m.prev = da.BEGIN_GRADIENT_FILL;
                var K = m;
                z.context.fillStyle = z.createGradientPattern(K.buffer.o[K.oPos], K.buffer.ii[K.iiPos], K.buffer.ff[K.ffPos], K.buffer.ii[K.iiPos + 1], K.buffer.o[K.oPos + 1], K.buffer.o[K.oPos + 2], K.buffer.o[K.oPos + 3], K.buffer.f[K.fPos]);
                z.bitmapFill = null;
                z.setSmoothing(!0);
                z.hasFill = !0;
                break;
            case 3:
                switch (m.prev._hx_index) {
                    case 0:
                        m.oPos += 2;
                        m.bPos += 2;
                        break;
                    case 1:
                        m.iPos += 1;
                        m.fPos += 1;
                        break;
                    case 2:
                        m.oPos += 4;
                        m.iiPos += 2;
                        m.ffPos += 1;
                        m.fPos += 1;
                        break;
                    case 3:
                        m.oPos += 1;
                        break;
                    case 4:
                        m.fPos += 6;
                        break;
                    case 5:
                        m.fPos += 4;
                        break;
                    case 6:
                        m.fPos +=
                            3;
                        break;
                    case 7:
                        m.fPos += 4;
                        break;
                    case 8:
                        m.oPos += 3;
                        break;
                    case 9:
                        m.fPos += 4;
                        break;
                    case 10:
                        m.fPos += 5;
                        m.oPos += 1;
                        break;
                    case 12:
                        m.oPos += 4;
                        break;
                    case 14:
                        m.oPos += 2;
                        m.bPos += 2;
                        break;
                    case 15:
                        m.oPos += 4;
                        m.iiPos += 2;
                        m.ffPos += 1;
                        m.fPos += 1;
                        break;
                    case 16:
                        m.oPos += 4;
                        m.iPos += 1;
                        m.fPos += 2;
                        m.bPos += 1;
                        break;
                    case 17:
                        m.fPos += 2;
                        break;
                    case 18:
                        m.fPos += 2;
                        break;
                    case 19:
                        m.oPos += 1;
                        break;
                    case 20:
                        m.oPos += 1
                }
                m.prev = da.BEGIN_SHADER_FILL;
                var Va = m,
                    E = Va.buffer.o[Va.oPos];
                if (0 < E.inputCount) {
                    z.bitmapFill = E.inputs[0];
                    if (z.bitmapFill.readable) z.context.fillStyle =
                        z.createBitmapFill(z.bitmapFill, 0 != E.inputWrap[0], 5 != E.inputFilter[0]);
                    else {
                        var sa = O.hex(0, 6);
                        z.context.fillStyle = "#" + sa
                    }
                    z.hasFill = !0;
                    z.pendingMatrix = null;
                    z.inversePendingMatrix = null
                }
                break;
            case 4:
                switch (m.prev._hx_index) {
                    case 0:
                        m.oPos += 2;
                        m.bPos += 2;
                        break;
                    case 1:
                        m.iPos += 1;
                        m.fPos += 1;
                        break;
                    case 2:
                        m.oPos += 4;
                        m.iiPos += 2;
                        m.ffPos += 1;
                        m.fPos += 1;
                        break;
                    case 3:
                        m.oPos += 1;
                        break;
                    case 4:
                        m.fPos += 6;
                        break;
                    case 5:
                        m.fPos += 4;
                        break;
                    case 6:
                        m.fPos += 3;
                        break;
                    case 7:
                        m.fPos += 4;
                        break;
                    case 8:
                        m.oPos += 3;
                        break;
                    case 9:
                        m.fPos += 4;
                        break;
                    case 10:
                        m.fPos += 5;
                        m.oPos += 1;
                        break;
                    case 12:
                        m.oPos += 4;
                        break;
                    case 14:
                        m.oPos += 2;
                        m.bPos += 2;
                        break;
                    case 15:
                        m.oPos += 4;
                        m.iiPos += 2;
                        m.ffPos += 1;
                        m.fPos += 1;
                        break;
                    case 16:
                        m.oPos += 4;
                        m.iPos += 1;
                        m.fPos += 2;
                        m.bPos += 1;
                        break;
                    case 17:
                        m.fPos += 2;
                        break;
                    case 18:
                        m.fPos += 2;
                        break;
                    case 19:
                        m.oPos += 1;
                        break;
                    case 20:
                        m.oPos += 1
                }
                m.prev = da.CUBIC_CURVE_TO;
                var Y = m;
                u = !0;
                z.context.bezierCurveTo(Y.buffer.f[Y.fPos] - c, Y.buffer.f[Y.fPos + 1] - d, Y.buffer.f[Y.fPos + 2] - c, Y.buffer.f[Y.fPos + 3] - d, Y.buffer.f[Y.fPos + 4] - c, Y.buffer.f[Y.fPos + 5] - d);
                f = Y.buffer.f[Y.fPos +
                    4];
                h = Y.buffer.f[Y.fPos + 5];
                break;
            case 5:
                switch (m.prev._hx_index) {
                    case 0:
                        m.oPos += 2;
                        m.bPos += 2;
                        break;
                    case 1:
                        m.iPos += 1;
                        m.fPos += 1;
                        break;
                    case 2:
                        m.oPos += 4;
                        m.iiPos += 2;
                        m.ffPos += 1;
                        m.fPos += 1;
                        break;
                    case 3:
                        m.oPos += 1;
                        break;
                    case 4:
                        m.fPos += 6;
                        break;
                    case 5:
                        m.fPos += 4;
                        break;
                    case 6:
                        m.fPos += 3;
                        break;
                    case 7:
                        m.fPos += 4;
                        break;
                    case 8:
                        m.oPos += 3;
                        break;
                    case 9:
                        m.fPos += 4;
                        break;
                    case 10:
                        m.fPos += 5;
                        m.oPos += 1;
                        break;
                    case 12:
                        m.oPos += 4;
                        break;
                    case 14:
                        m.oPos += 2;
                        m.bPos += 2;
                        break;
                    case 15:
                        m.oPos += 4;
                        m.iiPos += 2;
                        m.ffPos += 1;
                        m.fPos += 1;
                        break;
                    case 16:
                        m.oPos +=
                            4;
                        m.iPos += 1;
                        m.fPos += 2;
                        m.bPos += 1;
                        break;
                    case 17:
                        m.fPos += 2;
                        break;
                    case 18:
                        m.fPos += 2;
                        break;
                    case 19:
                        m.oPos += 1;
                        break;
                    case 20:
                        m.oPos += 1
                }
                m.prev = da.CURVE_TO;
                var W = m;
                u = !0;
                z.context.quadraticCurveTo(W.buffer.f[W.fPos] - c, W.buffer.f[W.fPos + 1] - d, W.buffer.f[W.fPos + 2] - c, W.buffer.f[W.fPos + 3] - d);
                f = W.buffer.f[W.fPos + 2];
                h = W.buffer.f[W.fPos + 3];
                break;
            case 6:
                switch (m.prev._hx_index) {
                    case 0:
                        m.oPos += 2;
                        m.bPos += 2;
                        break;
                    case 1:
                        m.iPos += 1;
                        m.fPos += 1;
                        break;
                    case 2:
                        m.oPos += 4;
                        m.iiPos += 2;
                        m.ffPos += 1;
                        m.fPos += 1;
                        break;
                    case 3:
                        m.oPos += 1;
                        break;
                    case 4:
                        m.fPos += 6;
                        break;
                    case 5:
                        m.fPos += 4;
                        break;
                    case 6:
                        m.fPos += 3;
                        break;
                    case 7:
                        m.fPos += 4;
                        break;
                    case 8:
                        m.oPos += 3;
                        break;
                    case 9:
                        m.fPos += 4;
                        break;
                    case 10:
                        m.fPos += 5;
                        m.oPos += 1;
                        break;
                    case 12:
                        m.oPos += 4;
                        break;
                    case 14:
                        m.oPos += 2;
                        m.bPos += 2;
                        break;
                    case 15:
                        m.oPos += 4;
                        m.iiPos += 2;
                        m.ffPos += 1;
                        m.fPos += 1;
                        break;
                    case 16:
                        m.oPos += 4;
                        m.iPos += 1;
                        m.fPos += 2;
                        m.bPos += 1;
                        break;
                    case 17:
                        m.fPos += 2;
                        break;
                    case 18:
                        m.fPos += 2;
                        break;
                    case 19:
                        m.oPos += 1;
                        break;
                    case 20:
                        m.oPos += 1
                }
                m.prev = da.DRAW_CIRCLE;
                var A = m;
                u = !0;
                z.context.moveTo(A.buffer.f[A.fPos] -
                    c + A.buffer.f[A.fPos + 2], A.buffer.f[A.fPos + 1] - d);
                z.context.arc(A.buffer.f[A.fPos] - c, A.buffer.f[A.fPos + 1] - d, A.buffer.f[A.fPos + 2], 0, 2 * Math.PI, !0);
                break;
            case 7:
                switch (m.prev._hx_index) {
                    case 0:
                        m.oPos += 2;
                        m.bPos += 2;
                        break;
                    case 1:
                        m.iPos += 1;
                        m.fPos += 1;
                        break;
                    case 2:
                        m.oPos += 4;
                        m.iiPos += 2;
                        m.ffPos += 1;
                        m.fPos += 1;
                        break;
                    case 3:
                        m.oPos += 1;
                        break;
                    case 4:
                        m.fPos += 6;
                        break;
                    case 5:
                        m.fPos += 4;
                        break;
                    case 6:
                        m.fPos += 3;
                        break;
                    case 7:
                        m.fPos += 4;
                        break;
                    case 8:
                        m.oPos += 3;
                        break;
                    case 9:
                        m.fPos += 4;
                        break;
                    case 10:
                        m.fPos += 5;
                        m.oPos += 1;
                        break;
                    case 12:
                        m.oPos +=
                            4;
                        break;
                    case 14:
                        m.oPos += 2;
                        m.bPos += 2;
                        break;
                    case 15:
                        m.oPos += 4;
                        m.iiPos += 2;
                        m.ffPos += 1;
                        m.fPos += 1;
                        break;
                    case 16:
                        m.oPos += 4;
                        m.iPos += 1;
                        m.fPos += 2;
                        m.bPos += 1;
                        break;
                    case 17:
                        m.fPos += 2;
                        break;
                    case 18:
                        m.fPos += 2;
                        break;
                    case 19:
                        m.oPos += 1;
                        break;
                    case 20:
                        m.oPos += 1
                }
                m.prev = da.DRAW_ELLIPSE;
                var Za = m;
                u = !0;
                var cb = Za.buffer.f[Za.fPos];
                var B = Za.buffer.f[Za.fPos + 1];
                var L = Za.buffer.f[Za.fPos + 2];
                var N = Za.buffer.f[Za.fPos + 3];
                cb -= c;
                B -= d;
                var aa = L / 2 * .5522848;
                var U = N / 2 * .5522848;
                var $a = cb + L;
                var R = B + N;
                var S = cb + L / 2;
                var V = B + N / 2;
                z.context.moveTo(cb,
                    V);
                z.context.bezierCurveTo(cb, V - U, S - aa, B, S, B);
                z.context.bezierCurveTo(S + aa, B, $a, V - U, $a, V);
                z.context.bezierCurveTo($a, V + U, S + aa, R, S, R);
                z.context.bezierCurveTo(S - aa, R, cb, V + U, cb, V);
                break;
            case 8:
                switch (m.prev._hx_index) {
                    case 0:
                        m.oPos += 2;
                        m.bPos += 2;
                        break;
                    case 1:
                        m.iPos += 1;
                        m.fPos += 1;
                        break;
                    case 2:
                        m.oPos += 4;
                        m.iiPos += 2;
                        m.ffPos += 1;
                        m.fPos += 1;
                        break;
                    case 3:
                        m.oPos += 1;
                        break;
                    case 4:
                        m.fPos += 6;
                        break;
                    case 5:
                        m.fPos += 4;
                        break;
                    case 6:
                        m.fPos += 3;
                        break;
                    case 7:
                        m.fPos += 4;
                        break;
                    case 8:
                        m.oPos += 3;
                        break;
                    case 9:
                        m.fPos += 4;
                        break;
                    case 10:
                        m.fPos +=
                            5;
                        m.oPos += 1;
                        break;
                    case 12:
                        m.oPos += 4;
                        break;
                    case 14:
                        m.oPos += 2;
                        m.bPos += 2;
                        break;
                    case 15:
                        m.oPos += 4;
                        m.iiPos += 2;
                        m.ffPos += 1;
                        m.fPos += 1;
                        break;
                    case 16:
                        m.oPos += 4;
                        m.iPos += 1;
                        m.fPos += 2;
                        m.bPos += 1;
                        break;
                    case 17:
                        m.fPos += 2;
                        break;
                    case 18:
                        m.fPos += 2;
                        break;
                    case 19:
                        m.oPos += 1;
                        break;
                    case 20:
                        m.oPos += 1
                }
                m.prev = da.DRAW_QUADS;
                var Cb = m,
                    ea = Cb.buffer.o[Cb.oPos],
                    X = Cb.buffer.o[Cb.oPos + 1],
                    Q = Cb.buffer.o[Cb.oPos + 2],
                    Z = null != X,
                    T = !1,
                    M = !1,
                    xa = Z ? X.get_length() : Math.floor(ea.get_length() / 4);
                if (0 == xa) return;
                null != Q && (Q.get_length() >= 6 * xa ? M = T = !0 : Q.get_length() >= 4 * xa ? T = !0 : Q.get_length() >= 2 * xa && (M = !0));
                var Oc = na.__pool.get(),
                    Pb = ua.__pool.get(),
                    pa = z.graphics.__renderTransform;
                z.context.save();
                for (var Ef = 0, ca = xa; Ef < ca;) {
                    var ab = Ef++;
                    var Od = Z ? 4 * X.get(ab) : 4 * ab;
                    if (!(0 > Od || (Oc.setTo(ea.get(Od), ea.get(Od + 1), ea.get(Od + 2), ea.get(Od + 3)), 0 >= Oc.width || 0 >= Oc.height))) {
                        if (T && M) {
                            var ba = 6 * ab;
                            Pb.setTo(Q.get(ba), Q.get(ba + 1), Q.get(ba + 2), Q.get(ba + 3), Q.get(ba + 4), Q.get(ba + 5))
                        } else T ? (ba = 4 * ab, Pb.setTo(Q.get(ba), Q.get(ba + 1), Q.get(ba + 2), Q.get(ba + 3), Oc.x, Oc.y)) : M ?
                            (ba = 2 * ab, Pb.tx = Q.get(ba), Pb.ty = Q.get(ba + 1)) : (Pb.tx = Oc.x, Pb.ty = Oc.y);
                        Pb.tx += f - c;
                        Pb.ty += h - d;
                        Pb.concat(pa);
                        z.context.setTransform(Pb.a, Pb.b, Pb.c, Pb.d, Pb.tx, Pb.ty);
                        null != z.bitmapFill && z.bitmapFill.readable ? z.context.drawImage(z.bitmapFill.image.get_src(), Oc.x, Oc.y, Oc.width, Oc.height, 0, 0, Oc.width, Oc.height) : z.context.fillRect(0, 0, Oc.width, Oc.height)
                    }
                }
                na.__pool.release(Oc);
                ua.__pool.release(Pb);
                z.context.restore();
                break;
            case 9:
                switch (m.prev._hx_index) {
                    case 0:
                        m.oPos += 2;
                        m.bPos += 2;
                        break;
                    case 1:
                        m.iPos += 1;
                        m.fPos +=
                            1;
                        break;
                    case 2:
                        m.oPos += 4;
                        m.iiPos += 2;
                        m.ffPos += 1;
                        m.fPos += 1;
                        break;
                    case 3:
                        m.oPos += 1;
                        break;
                    case 4:
                        m.fPos += 6;
                        break;
                    case 5:
                        m.fPos += 4;
                        break;
                    case 6:
                        m.fPos += 3;
                        break;
                    case 7:
                        m.fPos += 4;
                        break;
                    case 8:
                        m.oPos += 3;
                        break;
                    case 9:
                        m.fPos += 4;
                        break;
                    case 10:
                        m.fPos += 5;
                        m.oPos += 1;
                        break;
                    case 12:
                        m.oPos += 4;
                        break;
                    case 14:
                        m.oPos += 2;
                        m.bPos += 2;
                        break;
                    case 15:
                        m.oPos += 4;
                        m.iiPos += 2;
                        m.ffPos += 1;
                        m.fPos += 1;
                        break;
                    case 16:
                        m.oPos += 4;
                        m.iPos += 1;
                        m.fPos += 2;
                        m.bPos += 1;
                        break;
                    case 17:
                        m.fPos += 2;
                        break;
                    case 18:
                        m.fPos += 2;
                        break;
                    case 19:
                        m.oPos += 1;
                        break;
                    case 20:
                        m.oPos +=
                            1
                }
                m.prev = da.DRAW_RECT;
                var Pa = m;
                var fa = !1;
                if (null != z.bitmapFill && z.bitmapFill.readable && !z.hitTesting) {
                    var ia = D = l = r = 0;
                    var oa = !0;
                    if (null != z.pendingMatrix)
                        if (0 != z.pendingMatrix.b || 0 != z.pendingMatrix.c) oa = !1;
                        else {
                            null == x && (x = I.__pool.get());
                            null == w && (w = I.__pool.get());
                            x.setTo(Pa.buffer.f[Pa.fPos], Pa.buffer.f[Pa.fPos + 1]);
                            var ha = z.inversePendingMatrix,
                                ka = x.x,
                                ma = x.y;
                            x.x = ka * ha.a + ma * ha.c + ha.tx;
                            x.y = ka * ha.b + ma * ha.d + ha.ty;
                            w.setTo(Pa.buffer.f[Pa.fPos] + Pa.buffer.f[Pa.fPos + 2], Pa.buffer.f[Pa.fPos + 1] + Pa.buffer.f[Pa.fPos +
                                3]);
                            var vd = z.inversePendingMatrix,
                                Ff = w.x,
                                ra = w.y;
                            w.x = Ff * vd.a + ra * vd.c + vd.tx;
                            w.y = Ff * vd.b + ra * vd.d + vd.ty;
                            r = x.y;
                            ia = x.x;
                            D = w.y;
                            l = w.x
                        }
                    else r = Pa.buffer.f[Pa.fPos + 1], ia = Pa.buffer.f[Pa.fPos], D = Pa.buffer.f[Pa.fPos + 1] + Pa.buffer.f[Pa.fPos + 3], l = Pa.buffer.f[Pa.fPos] + Pa.buffer.f[Pa.fPos + 2];
                    oa && 0 <= r && 0 <= ia && l <= z.bitmapFill.width && D <= z.bitmapFill.height && (fa = !0, z.hitTesting || z.context.drawImage(z.bitmapFill.image.get_src(), ia, r, l - ia, D - r, Pa.buffer.f[Pa.fPos] - c, Pa.buffer.f[Pa.fPos + 1] - d, Pa.buffer.f[Pa.fPos + 2], Pa.buffer.f[Pa.fPos +
                        3]))
                }
                fa || (u = !0, z.context.rect(Pa.buffer.f[Pa.fPos] - c, Pa.buffer.f[Pa.fPos + 1] - d, Pa.buffer.f[Pa.fPos + 2], Pa.buffer.f[Pa.fPos + 3]));
                break;
            case 10:
                switch (m.prev._hx_index) {
                    case 0:
                        m.oPos += 2;
                        m.bPos += 2;
                        break;
                    case 1:
                        m.iPos += 1;
                        m.fPos += 1;
                        break;
                    case 2:
                        m.oPos += 4;
                        m.iiPos += 2;
                        m.ffPos += 1;
                        m.fPos += 1;
                        break;
                    case 3:
                        m.oPos += 1;
                        break;
                    case 4:
                        m.fPos += 6;
                        break;
                    case 5:
                        m.fPos += 4;
                        break;
                    case 6:
                        m.fPos += 3;
                        break;
                    case 7:
                        m.fPos += 4;
                        break;
                    case 8:
                        m.oPos += 3;
                        break;
                    case 9:
                        m.fPos += 4;
                        break;
                    case 10:
                        m.fPos += 5;
                        m.oPos += 1;
                        break;
                    case 12:
                        m.oPos += 4;
                        break;
                    case 14:
                        m.oPos += 2;
                        m.bPos += 2;
                        break;
                    case 15:
                        m.oPos += 4;
                        m.iiPos += 2;
                        m.ffPos += 1;
                        m.fPos += 1;
                        break;
                    case 16:
                        m.oPos += 4;
                        m.iPos += 1;
                        m.fPos += 2;
                        m.bPos += 1;
                        break;
                    case 17:
                        m.fPos += 2;
                        break;
                    case 18:
                        m.fPos += 2;
                        break;
                    case 19:
                        m.oPos += 1;
                        break;
                    case 20:
                        m.oPos += 1
                }
                m.prev = da.DRAW_ROUND_RECT;
                var ja = m;
                u = !0;
                z.drawRoundRect(ja.buffer.f[ja.fPos] - c, ja.buffer.f[ja.fPos + 1] - d, ja.buffer.f[ja.fPos + 2], ja.buffer.f[ja.fPos + 3], ja.buffer.f[ja.fPos + 4], ja.buffer.o[ja.oPos]);
                break;
            case 12:
                switch (m.prev._hx_index) {
                    case 0:
                        m.oPos += 2;
                        m.bPos += 2;
                        break;
                    case 1:
                        m.iPos += 1;
                        m.fPos += 1;
                        break;
                    case 2:
                        m.oPos += 4;
                        m.iiPos += 2;
                        m.ffPos += 1;
                        m.fPos += 1;
                        break;
                    case 3:
                        m.oPos += 1;
                        break;
                    case 4:
                        m.fPos += 6;
                        break;
                    case 5:
                        m.fPos += 4;
                        break;
                    case 6:
                        m.fPos += 3;
                        break;
                    case 7:
                        m.fPos += 4;
                        break;
                    case 8:
                        m.oPos += 3;
                        break;
                    case 9:
                        m.fPos += 4;
                        break;
                    case 10:
                        m.fPos += 5;
                        m.oPos += 1;
                        break;
                    case 12:
                        m.oPos += 4;
                        break;
                    case 14:
                        m.oPos += 2;
                        m.bPos += 2;
                        break;
                    case 15:
                        m.oPos += 4;
                        m.iiPos += 2;
                        m.ffPos += 1;
                        m.fPos += 1;
                        break;
                    case 16:
                        m.oPos += 4;
                        m.iPos += 1;
                        m.fPos += 2;
                        m.bPos += 1;
                        break;
                    case 17:
                        m.fPos += 2;
                        break;
                    case 18:
                        m.fPos += 2;
                        break;
                    case 19:
                        m.oPos +=
                            1;
                        break;
                    case 20:
                        m.oPos += 1
                }
                m.prev = da.DRAW_TRIANGLES;
                var fd = m,
                    Hb = fd.buffer.o[fd.oPos],
                    wa = fd.buffer.o[fd.oPos + 1],
                    qa = fd.buffer.o[fd.oPos + 2],
                    va = null,
                    ob = null == z.bitmapFill;
                if (ob && null != qa) break a;
                if (!ob) {
                    if (null == qa) {
                        qa = la.toFloatVector(null);
                        for (var ya = 0, gb = Hb.get_length() / 2 | 0; ya < gb;) {
                            var Oa = ya++;
                            qa.push(Hb.get(2 * Oa) - c / z.bitmapFill.width);
                            qa.push(Hb.get(2 * Oa + 1) - d / z.bitmapFill.height)
                        }
                    }
                    var Ba = qa.get_length() != Hb.get_length(),
                        kb = z.normalizeUVT(qa, Ba),
                        bb = kb.max;
                    qa = kb.uvt;
                    va = 1 < bb ? z.createTempPatternCanvas(z.bitmapFill,
                        z.bitmapRepeat, z.bounds.width | 0, z.bounds.height | 0) : z.createTempPatternCanvas(z.bitmapFill, z.bitmapRepeat, z.bitmapFill.width, z.bitmapFill.height)
                }
                for (var za = 0, Wa = wa.get_length(), fb, Fa, mb, Ja, Ka, La, Ma, Qa, xc, Ca, Ea, Ga, Aa, Sb, Ha, Ra, Ia, Na, ta, Sa, Ta, Ua, db, sb, Ab, ib, Id, jb; za < Wa;) {
                    fb = za;
                    Fa = za + 1;
                    mb = za + 2;
                    Ja = 2 * wa.get(fb);
                    Ka = 2 * wa.get(fb) + 1;
                    La = 2 * wa.get(Fa);
                    Ma = 2 * wa.get(Fa) + 1;
                    Qa = 2 * wa.get(mb);
                    xc = 2 * wa.get(mb) + 1;
                    Ca = Hb.get(Ja) - c;
                    Ea = Hb.get(Ka) - d;
                    Ga = Hb.get(La) - c;
                    Aa = Hb.get(Ma) - d;
                    Sb = Hb.get(Qa) - c;
                    Ha = Hb.get(xc) - d;
                    switch (fd.buffer.o[fd.oPos +
                            3]) {
                        case 0:
                            if (0 > (Ga - Ca) * (Ha - Ea) - (Aa - Ea) * (Sb - Ca)) {
                                za += 3;
                                continue
                            }
                            break;
                        case 2:
                            if (!(0 > (Ga - Ca) * (Ha - Ea) - (Aa - Ea) * (Sb - Ca))) {
                                za += 3;
                                continue
                            }
                    }
                    ob ? (z.context.beginPath(), z.context.moveTo(Ca, Ea), z.context.lineTo(Ga, Aa), z.context.lineTo(Sb, Ha), z.context.closePath(), z.hitTesting || z.context.fill(z.windingRule), za += 3) : (Ra = qa.get(Ja) * va.width, Na = qa.get(La) * va.width, Sa = qa.get(Qa) * va.width, Ia = qa.get(Ka) * va.height, ta = qa.get(Ma) * va.height, Ta = qa.get(xc) * va.height, Ua = Ra * (Ta - ta) - Na * Ta + Sa * ta + (Na - Sa) * Ia, 0 == Ua ? (za += 3, z.context.restore()) :
                        (z.context.save(), z.context.beginPath(), z.context.moveTo(Ca, Ea), z.context.lineTo(Ga, Aa), z.context.lineTo(Sb, Ha), z.context.closePath(), z.context.clip(), db = -(Ia * (Sb - Ga) - ta * Sb + Ta * Ga + (ta - Ta) * Ca) / Ua, sb = (ta * Ha + Ia * (Aa - Ha) - Ta * Aa + (Ta - ta) * Ea) / Ua, Ab = (Ra * (Sb - Ga) - Na * Sb + Sa * Ga + (Na - Sa) * Ca) / Ua, ib = -(Na * Ha + Ra * (Aa - Ha) - Sa * Aa + (Sa - Na) * Ea) / Ua, Id = (Ra * (Ta * Ga - ta * Sb) + Ia * (Na * Sb - Sa * Ga) + (Sa * ta - Na * Ta) * Ca) / Ua, jb = (Ra * (Ta * Aa - ta * Ha) + Ia * (Na * Ha - Sa * Aa) + (Sa * ta - Na * Ta) * Ea) / Ua, z.context.transform(db, sb, Ab, ib, Id, jb), z.context.drawImage(va,
                            0, 0, va.width, va.height), z.context.restore(), za += 3))
                }
                break;
            case 14:
                switch (m.prev._hx_index) {
                    case 0:
                        m.oPos += 2;
                        m.bPos += 2;
                        break;
                    case 1:
                        m.iPos += 1;
                        m.fPos += 1;
                        break;
                    case 2:
                        m.oPos += 4;
                        m.iiPos += 2;
                        m.ffPos += 1;
                        m.fPos += 1;
                        break;
                    case 3:
                        m.oPos += 1;
                        break;
                    case 4:
                        m.fPos += 6;
                        break;
                    case 5:
                        m.fPos += 4;
                        break;
                    case 6:
                        m.fPos += 3;
                        break;
                    case 7:
                        m.fPos += 4;
                        break;
                    case 8:
                        m.oPos += 3;
                        break;
                    case 9:
                        m.fPos += 4;
                        break;
                    case 10:
                        m.fPos += 5;
                        m.oPos += 1;
                        break;
                    case 12:
                        m.oPos += 4;
                        break;
                    case 14:
                        m.oPos += 2;
                        m.bPos += 2;
                        break;
                    case 15:
                        m.oPos += 4;
                        m.iiPos += 2;
                        m.ffPos +=
                            1;
                        m.fPos += 1;
                        break;
                    case 16:
                        m.oPos += 4;
                        m.iPos += 1;
                        m.fPos += 2;
                        m.bPos += 1;
                        break;
                    case 17:
                        m.fPos += 2;
                        break;
                    case 18:
                        m.fPos += 2;
                        break;
                    case 19:
                        m.oPos += 1;
                        break;
                    case 20:
                        m.oPos += 1
                }
                m.prev = da.LINE_BITMAP_STYLE;
                var nb = m;
                b && z.hasStroke && z.closePath(!0);
                z.context.moveTo(f - c, h - d);
                if (nb.buffer.o[nb.oPos].readable) z.context.strokeStyle = z.createBitmapFill(nb.buffer.o[nb.oPos], nb.buffer.b[nb.bPos], nb.buffer.b[nb.bPos + 1]);
                else {
                    var Le = O.hex(0, 6);
                    z.context.strokeStyle = "#" + Le
                }
                z.hasStroke = !0;
                break;
            case 15:
                switch (m.prev._hx_index) {
                    case 0:
                        m.oPos +=
                            2;
                        m.bPos += 2;
                        break;
                    case 1:
                        m.iPos += 1;
                        m.fPos += 1;
                        break;
                    case 2:
                        m.oPos += 4;
                        m.iiPos += 2;
                        m.ffPos += 1;
                        m.fPos += 1;
                        break;
                    case 3:
                        m.oPos += 1;
                        break;
                    case 4:
                        m.fPos += 6;
                        break;
                    case 5:
                        m.fPos += 4;
                        break;
                    case 6:
                        m.fPos += 3;
                        break;
                    case 7:
                        m.fPos += 4;
                        break;
                    case 8:
                        m.oPos += 3;
                        break;
                    case 9:
                        m.fPos += 4;
                        break;
                    case 10:
                        m.fPos += 5;
                        m.oPos += 1;
                        break;
                    case 12:
                        m.oPos += 4;
                        break;
                    case 14:
                        m.oPos += 2;
                        m.bPos += 2;
                        break;
                    case 15:
                        m.oPos += 4;
                        m.iiPos += 2;
                        m.ffPos += 1;
                        m.fPos += 1;
                        break;
                    case 16:
                        m.oPos += 4;
                        m.iPos += 1;
                        m.fPos += 2;
                        m.bPos += 1;
                        break;
                    case 17:
                        m.fPos += 2;
                        break;
                    case 18:
                        m.fPos +=
                            2;
                        break;
                    case 19:
                        m.oPos += 1;
                        break;
                    case 20:
                        m.oPos += 1
                }
                m.prev = da.LINE_GRADIENT_STYLE;
                var Xa = m;
                b && z.hasStroke && z.closePath(!0);
                z.context.moveTo(f - c, h - d);
                z.context.strokeStyle = z.createGradientPattern(Xa.buffer.o[Xa.oPos], Xa.buffer.ii[Xa.iiPos], Xa.buffer.ff[Xa.ffPos], Xa.buffer.ii[Xa.iiPos + 1], Xa.buffer.o[Xa.oPos + 1], Xa.buffer.o[Xa.oPos + 2], Xa.buffer.o[Xa.oPos + 3], Xa.buffer.f[Xa.fPos]);
                z.setSmoothing(!0);
                z.hasStroke = !0;
                break;
            case 16:
                switch (m.prev._hx_index) {
                    case 0:
                        m.oPos += 2;
                        m.bPos += 2;
                        break;
                    case 1:
                        m.iPos += 1;
                        m.fPos +=
                            1;
                        break;
                    case 2:
                        m.oPos += 4;
                        m.iiPos += 2;
                        m.ffPos += 1;
                        m.fPos += 1;
                        break;
                    case 3:
                        m.oPos += 1;
                        break;
                    case 4:
                        m.fPos += 6;
                        break;
                    case 5:
                        m.fPos += 4;
                        break;
                    case 6:
                        m.fPos += 3;
                        break;
                    case 7:
                        m.fPos += 4;
                        break;
                    case 8:
                        m.oPos += 3;
                        break;
                    case 9:
                        m.fPos += 4;
                        break;
                    case 10:
                        m.fPos += 5;
                        m.oPos += 1;
                        break;
                    case 12:
                        m.oPos += 4;
                        break;
                    case 14:
                        m.oPos += 2;
                        m.bPos += 2;
                        break;
                    case 15:
                        m.oPos += 4;
                        m.iiPos += 2;
                        m.ffPos += 1;
                        m.fPos += 1;
                        break;
                    case 16:
                        m.oPos += 4;
                        m.iPos += 1;
                        m.fPos += 2;
                        m.bPos += 1;
                        break;
                    case 17:
                        m.fPos += 2;
                        break;
                    case 18:
                        m.fPos += 2;
                        break;
                    case 19:
                        m.oPos += 1;
                        break;
                    case 20:
                        m.oPos +=
                            1
                }
                m.prev = da.LINE_STYLE;
                var Da = m;
                b && z.hasStroke && z.closePath(!0);
                z.context.moveTo(f - c, h - d);
                if (null == Da.buffer.o[Da.oPos]) z.hasStroke = !1;
                else {
                    z.context.lineWidth = 0 < Da.buffer.o[Da.oPos] ? Da.buffer.o[Da.oPos] : 1;
                    var pb = null == Da.buffer.o[Da.oPos + 3] ? "round" : (null == Da.buffer.o[Da.oPos + 3] ? "null" : Ml.toString(Da.buffer.o[Da.oPos + 3])).toLowerCase();
                    z.context.lineJoin = pb;
                    var qb = null == Da.buffer.o[Da.oPos + 2] ? "round" : 0 == Da.buffer.o[Da.oPos + 2] ? "butt" : (null == Da.buffer.o[Da.oPos + 2] ? "null" : Ll.toString(Da.buffer.o[Da.oPos +
                        2])).toLowerCase();
                    z.context.lineCap = qb;
                    z.context.miterLimit = Da.buffer.f[Da.fPos + 1];
                    if (1 == Da.buffer.f[Da.fPos]) {
                        var rb = O.hex(Da.buffer.i[Da.iPos] & 16777215, 6);
                        z.context.strokeStyle = "#" + rb
                    } else eb = (Da.buffer.i[Da.iPos] & 16711680) >>> 16, H = (Da.buffer.i[Da.iPos] & 65280) >>> 8, lb = Da.buffer.i[Da.iPos] & 255, z.context.strokeStyle = "rgba(" + eb + ", " + H + ", " + lb + ", " + Da.buffer.f[Da.fPos] + ")";
                    z.setSmoothing(!0);
                    z.hasStroke = !0
                }
                break;
            case 17:
                switch (m.prev._hx_index) {
                    case 0:
                        m.oPos += 2;
                        m.bPos += 2;
                        break;
                    case 1:
                        m.iPos += 1;
                        m.fPos +=
                            1;
                        break;
                    case 2:
                        m.oPos += 4;
                        m.iiPos += 2;
                        m.ffPos += 1;
                        m.fPos += 1;
                        break;
                    case 3:
                        m.oPos += 1;
                        break;
                    case 4:
                        m.fPos += 6;
                        break;
                    case 5:
                        m.fPos += 4;
                        break;
                    case 6:
                        m.fPos += 3;
                        break;
                    case 7:
                        m.fPos += 4;
                        break;
                    case 8:
                        m.oPos += 3;
                        break;
                    case 9:
                        m.fPos += 4;
                        break;
                    case 10:
                        m.fPos += 5;
                        m.oPos += 1;
                        break;
                    case 12:
                        m.oPos += 4;
                        break;
                    case 14:
                        m.oPos += 2;
                        m.bPos += 2;
                        break;
                    case 15:
                        m.oPos += 4;
                        m.iiPos += 2;
                        m.ffPos += 1;
                        m.fPos += 1;
                        break;
                    case 16:
                        m.oPos += 4;
                        m.iPos += 1;
                        m.fPos += 2;
                        m.bPos += 1;
                        break;
                    case 17:
                        m.fPos += 2;
                        break;
                    case 18:
                        m.fPos += 2;
                        break;
                    case 19:
                        m.oPos += 1;
                        break;
                    case 20:
                        m.oPos +=
                            1
                }
                m.prev = da.LINE_TO;
                var hb = m;
                u = !0;
                z.context.lineTo(hb.buffer.f[hb.fPos] - c, hb.buffer.f[hb.fPos + 1] - d);
                f = hb.buffer.f[hb.fPos];
                h = hb.buffer.f[hb.fPos + 1];
                f == g && h == p && (k = !0);
                break;
            case 18:
                switch (m.prev._hx_index) {
                    case 0:
                        m.oPos += 2;
                        m.bPos += 2;
                        break;
                    case 1:
                        m.iPos += 1;
                        m.fPos += 1;
                        break;
                    case 2:
                        m.oPos += 4;
                        m.iiPos += 2;
                        m.ffPos += 1;
                        m.fPos += 1;
                        break;
                    case 3:
                        m.oPos += 1;
                        break;
                    case 4:
                        m.fPos += 6;
                        break;
                    case 5:
                        m.fPos += 4;
                        break;
                    case 6:
                        m.fPos += 3;
                        break;
                    case 7:
                        m.fPos += 4;
                        break;
                    case 8:
                        m.oPos += 3;
                        break;
                    case 9:
                        m.fPos += 4;
                        break;
                    case 10:
                        m.fPos +=
                            5;
                        m.oPos += 1;
                        break;
                    case 12:
                        m.oPos += 4;
                        break;
                    case 14:
                        m.oPos += 2;
                        m.bPos += 2;
                        break;
                    case 15:
                        m.oPos += 4;
                        m.iiPos += 2;
                        m.ffPos += 1;
                        m.fPos += 1;
                        break;
                    case 16:
                        m.oPos += 4;
                        m.iPos += 1;
                        m.fPos += 2;
                        m.bPos += 1;
                        break;
                    case 17:
                        m.fPos += 2;
                        break;
                    case 18:
                        m.fPos += 2;
                        break;
                    case 19:
                        m.oPos += 1;
                        break;
                    case 20:
                        m.oPos += 1
                }
                m.prev = da.MOVE_TO;
                var Ya = m;
                z.context.moveTo(Ya.buffer.f[Ya.fPos] - c, Ya.buffer.f[Ya.fPos + 1] - d);
                f = Ya.buffer.f[Ya.fPos];
                h = Ya.buffer.f[Ya.fPos + 1];
                q && Ya.buffer.f[Ya.fPos] != g && Ya.buffer.f[Ya.fPos + 1] != p && (k = !0);
                g = Ya.buffer.f[Ya.fPos];
                p = Ya.buffer.f[Ya.fPos + 1];
                q = !0;
                break;
            case 21:
                z.windingRule = "evenodd";
                break;
            case 22:
                z.windingRule = "nonzero";
                break;
            default:
                switch (m.prev._hx_index) {
                    case 0:
                        m.oPos += 2;
                        m.bPos += 2;
                        break;
                    case 1:
                        m.iPos += 1;
                        m.fPos += 1;
                        break;
                    case 2:
                        m.oPos += 4;
                        m.iiPos += 2;
                        m.ffPos += 1;
                        m.fPos += 1;
                        break;
                    case 3:
                        m.oPos += 1;
                        break;
                    case 4:
                        m.fPos += 6;
                        break;
                    case 5:
                        m.fPos += 4;
                        break;
                    case 6:
                        m.fPos += 3;
                        break;
                    case 7:
                        m.fPos += 4;
                        break;
                    case 8:
                        m.oPos += 3;
                        break;
                    case 9:
                        m.fPos += 4;
                        break;
                    case 10:
                        m.fPos += 5;
                        m.oPos += 1;
                        break;
                    case 12:
                        m.oPos += 4;
                        break;
                    case 14:
                        m.oPos +=
                            2;
                        m.bPos += 2;
                        break;
                    case 15:
                        m.oPos += 4;
                        m.iiPos += 2;
                        m.ffPos += 1;
                        m.fPos += 1;
                        break;
                    case 16:
                        m.oPos += 4;
                        m.iPos += 1;
                        m.fPos += 2;
                        m.bPos += 1;
                        break;
                    case 17:
                        m.fPos += 2;
                        break;
                    case 18:
                        m.fPos += 2;
                        break;
                    case 19:
                        m.oPos += 1;
                        break;
                    case 20:
                        m.oPos += 1
                }
                m.prev = C
        }
    }
    null != x && I.__pool.release(x);
    null != w && I.__pool.release(w);
    m.destroy();
    if (u) {
        if (b && z.hasStroke) {
            if (z.hasFill) {
                if (f != g || h != p) z.context.lineTo(g - c, p - d), k = !0;
                k && z.closePath(!0)
            } else k && f == g && h == p && z.closePath(!0);
            z.hitTesting || z.context.stroke()
        }
        b || !z.hasFill && null == z.bitmapFill ||
            (z.context.translate(-z.bounds.x, -z.bounds.y), null != z.pendingMatrix ? (z.context.transform(z.pendingMatrix.a, z.pendingMatrix.b, z.pendingMatrix.c, z.pendingMatrix.d, z.pendingMatrix.tx, z.pendingMatrix.ty), z.hitTesting || z.context.fill(z.windingRule), z.context.transform(z.inversePendingMatrix.a, z.inversePendingMatrix.b, z.inversePendingMatrix.c, z.inversePendingMatrix.d, z.inversePendingMatrix.tx, z.inversePendingMatrix.ty)) : z.hitTesting || z.context.fill(z.windingRule), z.context.translate(z.bounds.x, z.bounds.y),
                z.context.closePath())
    }
};
z.render = function(a, b) {
    a.__update(b.__worldTransform, b.__pixelRatio);
    if (a.__softwareDirty) {
        z.hitTesting = !1;
        z.graphics = a;
        z.allowSmoothing = b.__allowSmoothing;
        z.worldAlpha = b.__getAlpha(a.__owner.__worldAlpha);
        z.bounds = a.__bounds;
        var c = a.__width,
            d = a.__height;
        if (!a.__visible || 0 == a.__commands.get_length() || null == z.bounds || 1 > c || 1 > d) a.__canvas = null, a.__context = null, a.__bitmap = null;
        else {
            null == a.__canvas && (a.__canvas = window.document.createElement("canvas"), a.__context = a.__canvas.getContext("2d"));
            z.context = a.__context;
            var f = a.__renderTransform,
                h = a.__canvas,
                k = b.__pixelRatio,
                g = c * k | 0,
                p = d * k | 0;
            b.__setBlendModeContext(z.context, 10);
            b.__isDOM ? (h.width == g && h.height == p ? z.context.clearRect(0, 0, g, p) : (h.width = g, h.height = p, h.style.width = c + "px", h.style.height = d + "px"), c = a.__renderTransform, z.context.setTransform(c.a * k, c.b * k, c.c * k, c.d * k, c.tx * k, c.ty * k)) : (h.width == g && h.height == p ? (z.context.closePath(), z.context.setTransform(1, 0, 0, 1, 0, 0), z.context.clearRect(0, 0, g, p)) : (h.width = c, h.height = d), z.context.setTransform(f.a,
                f.b, f.c, f.d, f.tx, f.ty));
            z.fillCommands.clear();
            z.strokeCommands.clear();
            z.hasFill = !1;
            z.hasStroke = !1;
            z.bitmapFill = null;
            k = z.bitmapRepeat = !1;
            d = c = 0;
            z.windingRule = "evenodd";
            f = new ue(a.__commands);
            h = 0;
            for (g = a.__commands.types; h < g.length;) switch (p = g[h], ++h, p._hx_index) {
                case 0:
                case 1:
                case 2:
                case 3:
                    z.endFill();
                    z.endStroke();
                    if (p == da.BEGIN_BITMAP_FILL) {
                        switch (f.prev._hx_index) {
                            case 0:
                                f.oPos += 2;
                                f.bPos += 2;
                                break;
                            case 1:
                                f.iPos += 1;
                                f.fPos += 1;
                                break;
                            case 2:
                                f.oPos += 4;
                                f.iiPos += 2;
                                f.ffPos += 1;
                                f.fPos += 1;
                                break;
                            case 3:
                                f.oPos +=
                                    1;
                                break;
                            case 4:
                                f.fPos += 6;
                                break;
                            case 5:
                                f.fPos += 4;
                                break;
                            case 6:
                                f.fPos += 3;
                                break;
                            case 7:
                                f.fPos += 4;
                                break;
                            case 8:
                                f.oPos += 3;
                                break;
                            case 9:
                                f.fPos += 4;
                                break;
                            case 10:
                                f.fPos += 5;
                                f.oPos += 1;
                                break;
                            case 12:
                                f.oPos += 4;
                                break;
                            case 14:
                                f.oPos += 2;
                                f.bPos += 2;
                                break;
                            case 15:
                                f.oPos += 4;
                                f.iiPos += 2;
                                f.ffPos += 1;
                                f.fPos += 1;
                                break;
                            case 16:
                                f.oPos += 4;
                                f.iPos += 1;
                                f.fPos += 2;
                                f.bPos += 1;
                                break;
                            case 17:
                                f.fPos += 2;
                                break;
                            case 18:
                                f.fPos += 2;
                                break;
                            case 19:
                                f.oPos += 1;
                                break;
                            case 20:
                                f.oPos += 1
                        }
                        f.prev = da.BEGIN_BITMAP_FILL;
                        p = f;
                        z.fillCommands.beginBitmapFill(p.buffer.o[p.oPos],
                            p.buffer.o[p.oPos + 1], p.buffer.b[p.bPos], p.buffer.b[p.bPos + 1]);
                        z.strokeCommands.beginBitmapFill(p.buffer.o[p.oPos], p.buffer.o[p.oPos + 1], p.buffer.b[p.bPos], p.buffer.b[p.bPos + 1])
                    } else if (p == da.BEGIN_GRADIENT_FILL) {
                        switch (f.prev._hx_index) {
                            case 0:
                                f.oPos += 2;
                                f.bPos += 2;
                                break;
                            case 1:
                                f.iPos += 1;
                                f.fPos += 1;
                                break;
                            case 2:
                                f.oPos += 4;
                                f.iiPos += 2;
                                f.ffPos += 1;
                                f.fPos += 1;
                                break;
                            case 3:
                                f.oPos += 1;
                                break;
                            case 4:
                                f.fPos += 6;
                                break;
                            case 5:
                                f.fPos += 4;
                                break;
                            case 6:
                                f.fPos += 3;
                                break;
                            case 7:
                                f.fPos += 4;
                                break;
                            case 8:
                                f.oPos += 3;
                                break;
                            case 9:
                                f.fPos +=
                                    4;
                                break;
                            case 10:
                                f.fPos += 5;
                                f.oPos += 1;
                                break;
                            case 12:
                                f.oPos += 4;
                                break;
                            case 14:
                                f.oPos += 2;
                                f.bPos += 2;
                                break;
                            case 15:
                                f.oPos += 4;
                                f.iiPos += 2;
                                f.ffPos += 1;
                                f.fPos += 1;
                                break;
                            case 16:
                                f.oPos += 4;
                                f.iPos += 1;
                                f.fPos += 2;
                                f.bPos += 1;
                                break;
                            case 17:
                                f.fPos += 2;
                                break;
                            case 18:
                                f.fPos += 2;
                                break;
                            case 19:
                                f.oPos += 1;
                                break;
                            case 20:
                                f.oPos += 1
                        }
                        f.prev = da.BEGIN_GRADIENT_FILL;
                        p = f;
                        z.fillCommands.beginGradientFill(p.buffer.o[p.oPos], p.buffer.ii[p.iiPos], p.buffer.ff[p.ffPos], p.buffer.ii[p.iiPos + 1], p.buffer.o[p.oPos + 1], p.buffer.o[p.oPos + 2], p.buffer.o[p.oPos +
                            3], p.buffer.f[p.fPos]);
                        z.strokeCommands.beginGradientFill(p.buffer.o[p.oPos], p.buffer.ii[p.iiPos], p.buffer.ff[p.ffPos], p.buffer.ii[p.iiPos + 1], p.buffer.o[p.oPos + 1], p.buffer.o[p.oPos + 2], p.buffer.o[p.oPos + 3], p.buffer.f[p.fPos])
                    } else if (p == da.BEGIN_SHADER_FILL) {
                        switch (f.prev._hx_index) {
                            case 0:
                                f.oPos += 2;
                                f.bPos += 2;
                                break;
                            case 1:
                                f.iPos += 1;
                                f.fPos += 1;
                                break;
                            case 2:
                                f.oPos += 4;
                                f.iiPos += 2;
                                f.ffPos += 1;
                                f.fPos += 1;
                                break;
                            case 3:
                                f.oPos += 1;
                                break;
                            case 4:
                                f.fPos += 6;
                                break;
                            case 5:
                                f.fPos += 4;
                                break;
                            case 6:
                                f.fPos += 3;
                                break;
                            case 7:
                                f.fPos +=
                                    4;
                                break;
                            case 8:
                                f.oPos += 3;
                                break;
                            case 9:
                                f.fPos += 4;
                                break;
                            case 10:
                                f.fPos += 5;
                                f.oPos += 1;
                                break;
                            case 12:
                                f.oPos += 4;
                                break;
                            case 14:
                                f.oPos += 2;
                                f.bPos += 2;
                                break;
                            case 15:
                                f.oPos += 4;
                                f.iiPos += 2;
                                f.ffPos += 1;
                                f.fPos += 1;
                                break;
                            case 16:
                                f.oPos += 4;
                                f.iPos += 1;
                                f.fPos += 2;
                                f.bPos += 1;
                                break;
                            case 17:
                                f.fPos += 2;
                                break;
                            case 18:
                                f.fPos += 2;
                                break;
                            case 19:
                                f.oPos += 1;
                                break;
                            case 20:
                                f.oPos += 1
                        }
                        f.prev = da.BEGIN_SHADER_FILL;
                        p = f;
                        z.fillCommands.beginShaderFill(p.buffer.o[p.oPos]);
                        z.strokeCommands.beginShaderFill(p.buffer.o[p.oPos])
                    } else {
                        switch (f.prev._hx_index) {
                            case 0:
                                f.oPos +=
                                    2;
                                f.bPos += 2;
                                break;
                            case 1:
                                f.iPos += 1;
                                f.fPos += 1;
                                break;
                            case 2:
                                f.oPos += 4;
                                f.iiPos += 2;
                                f.ffPos += 1;
                                f.fPos += 1;
                                break;
                            case 3:
                                f.oPos += 1;
                                break;
                            case 4:
                                f.fPos += 6;
                                break;
                            case 5:
                                f.fPos += 4;
                                break;
                            case 6:
                                f.fPos += 3;
                                break;
                            case 7:
                                f.fPos += 4;
                                break;
                            case 8:
                                f.oPos += 3;
                                break;
                            case 9:
                                f.fPos += 4;
                                break;
                            case 10:
                                f.fPos += 5;
                                f.oPos += 1;
                                break;
                            case 12:
                                f.oPos += 4;
                                break;
                            case 14:
                                f.oPos += 2;
                                f.bPos += 2;
                                break;
                            case 15:
                                f.oPos += 4;
                                f.iiPos += 2;
                                f.ffPos += 1;
                                f.fPos += 1;
                                break;
                            case 16:
                                f.oPos += 4;
                                f.iPos += 1;
                                f.fPos += 2;
                                f.bPos += 1;
                                break;
                            case 17:
                                f.fPos += 2;
                                break;
                            case 18:
                                f.fPos +=
                                    2;
                                break;
                            case 19:
                                f.oPos += 1;
                                break;
                            case 20:
                                f.oPos += 1
                        }
                        f.prev = da.BEGIN_FILL;
                        p = f;
                        z.fillCommands.beginFill(p.buffer.i[p.iPos], p.buffer.f[p.fPos]);
                        z.strokeCommands.beginFill(p.buffer.i[p.iPos], p.buffer.f[p.fPos])
                    }
                    break;
                case 4:
                    switch (f.prev._hx_index) {
                        case 0:
                            f.oPos += 2;
                            f.bPos += 2;
                            break;
                        case 1:
                            f.iPos += 1;
                            f.fPos += 1;
                            break;
                        case 2:
                            f.oPos += 4;
                            f.iiPos += 2;
                            f.ffPos += 1;
                            f.fPos += 1;
                            break;
                        case 3:
                            f.oPos += 1;
                            break;
                        case 4:
                            f.fPos += 6;
                            break;
                        case 5:
                            f.fPos += 4;
                            break;
                        case 6:
                            f.fPos += 3;
                            break;
                        case 7:
                            f.fPos += 4;
                            break;
                        case 8:
                            f.oPos += 3;
                            break;
                        case 9:
                            f.fPos += 4;
                            break;
                        case 10:
                            f.fPos += 5;
                            f.oPos += 1;
                            break;
                        case 12:
                            f.oPos += 4;
                            break;
                        case 14:
                            f.oPos += 2;
                            f.bPos += 2;
                            break;
                        case 15:
                            f.oPos += 4;
                            f.iiPos += 2;
                            f.ffPos += 1;
                            f.fPos += 1;
                            break;
                        case 16:
                            f.oPos += 4;
                            f.iPos += 1;
                            f.fPos += 2;
                            f.bPos += 1;
                            break;
                        case 17:
                            f.fPos += 2;
                            break;
                        case 18:
                            f.fPos += 2;
                            break;
                        case 19:
                            f.oPos += 1;
                            break;
                        case 20:
                            f.oPos += 1
                    }
                    f.prev = da.CUBIC_CURVE_TO;
                    p = f;
                    z.fillCommands.cubicCurveTo(p.buffer.f[p.fPos], p.buffer.f[p.fPos + 1], p.buffer.f[p.fPos + 2], p.buffer.f[p.fPos + 3], p.buffer.f[p.fPos + 4], p.buffer.f[p.fPos + 5]);
                    k ? z.strokeCommands.cubicCurveTo(p.buffer.f[p.fPos],
                        p.buffer.f[p.fPos + 1], p.buffer.f[p.fPos + 2], p.buffer.f[p.fPos + 3], p.buffer.f[p.fPos + 4], p.buffer.f[p.fPos + 5]) : (c = p.buffer.f[p.fPos + 4], d = p.buffer.f[p.fPos + 5]);
                    break;
                case 5:
                    switch (f.prev._hx_index) {
                        case 0:
                            f.oPos += 2;
                            f.bPos += 2;
                            break;
                        case 1:
                            f.iPos += 1;
                            f.fPos += 1;
                            break;
                        case 2:
                            f.oPos += 4;
                            f.iiPos += 2;
                            f.ffPos += 1;
                            f.fPos += 1;
                            break;
                        case 3:
                            f.oPos += 1;
                            break;
                        case 4:
                            f.fPos += 6;
                            break;
                        case 5:
                            f.fPos += 4;
                            break;
                        case 6:
                            f.fPos += 3;
                            break;
                        case 7:
                            f.fPos += 4;
                            break;
                        case 8:
                            f.oPos += 3;
                            break;
                        case 9:
                            f.fPos += 4;
                            break;
                        case 10:
                            f.fPos += 5;
                            f.oPos += 1;
                            break;
                        case 12:
                            f.oPos += 4;
                            break;
                        case 14:
                            f.oPos += 2;
                            f.bPos += 2;
                            break;
                        case 15:
                            f.oPos += 4;
                            f.iiPos += 2;
                            f.ffPos += 1;
                            f.fPos += 1;
                            break;
                        case 16:
                            f.oPos += 4;
                            f.iPos += 1;
                            f.fPos += 2;
                            f.bPos += 1;
                            break;
                        case 17:
                            f.fPos += 2;
                            break;
                        case 18:
                            f.fPos += 2;
                            break;
                        case 19:
                            f.oPos += 1;
                            break;
                        case 20:
                            f.oPos += 1
                    }
                    f.prev = da.CURVE_TO;
                    p = f;
                    z.fillCommands.curveTo(p.buffer.f[p.fPos], p.buffer.f[p.fPos + 1], p.buffer.f[p.fPos + 2], p.buffer.f[p.fPos + 3]);
                    k ? z.strokeCommands.curveTo(p.buffer.f[p.fPos], p.buffer.f[p.fPos + 1], p.buffer.f[p.fPos + 2], p.buffer.f[p.fPos + 3]) :
                        (c = p.buffer.f[p.fPos + 2], d = p.buffer.f[p.fPos + 3]);
                    break;
                case 6:
                    switch (f.prev._hx_index) {
                        case 0:
                            f.oPos += 2;
                            f.bPos += 2;
                            break;
                        case 1:
                            f.iPos += 1;
                            f.fPos += 1;
                            break;
                        case 2:
                            f.oPos += 4;
                            f.iiPos += 2;
                            f.ffPos += 1;
                            f.fPos += 1;
                            break;
                        case 3:
                            f.oPos += 1;
                            break;
                        case 4:
                            f.fPos += 6;
                            break;
                        case 5:
                            f.fPos += 4;
                            break;
                        case 6:
                            f.fPos += 3;
                            break;
                        case 7:
                            f.fPos += 4;
                            break;
                        case 8:
                            f.oPos += 3;
                            break;
                        case 9:
                            f.fPos += 4;
                            break;
                        case 10:
                            f.fPos += 5;
                            f.oPos += 1;
                            break;
                        case 12:
                            f.oPos += 4;
                            break;
                        case 14:
                            f.oPos += 2;
                            f.bPos += 2;
                            break;
                        case 15:
                            f.oPos += 4;
                            f.iiPos += 2;
                            f.ffPos += 1;
                            f.fPos +=
                                1;
                            break;
                        case 16:
                            f.oPos += 4;
                            f.iPos += 1;
                            f.fPos += 2;
                            f.bPos += 1;
                            break;
                        case 17:
                            f.fPos += 2;
                            break;
                        case 18:
                            f.fPos += 2;
                            break;
                        case 19:
                            f.oPos += 1;
                            break;
                        case 20:
                            f.oPos += 1
                    }
                    f.prev = da.DRAW_CIRCLE;
                    p = f;
                    z.fillCommands.drawCircle(p.buffer.f[p.fPos], p.buffer.f[p.fPos + 1], p.buffer.f[p.fPos + 2]);
                    k && z.strokeCommands.drawCircle(p.buffer.f[p.fPos], p.buffer.f[p.fPos + 1], p.buffer.f[p.fPos + 2]);
                    break;
                case 7:
                    switch (f.prev._hx_index) {
                        case 0:
                            f.oPos += 2;
                            f.bPos += 2;
                            break;
                        case 1:
                            f.iPos += 1;
                            f.fPos += 1;
                            break;
                        case 2:
                            f.oPos += 4;
                            f.iiPos += 2;
                            f.ffPos += 1;
                            f.fPos += 1;
                            break;
                        case 3:
                            f.oPos += 1;
                            break;
                        case 4:
                            f.fPos += 6;
                            break;
                        case 5:
                            f.fPos += 4;
                            break;
                        case 6:
                            f.fPos += 3;
                            break;
                        case 7:
                            f.fPos += 4;
                            break;
                        case 8:
                            f.oPos += 3;
                            break;
                        case 9:
                            f.fPos += 4;
                            break;
                        case 10:
                            f.fPos += 5;
                            f.oPos += 1;
                            break;
                        case 12:
                            f.oPos += 4;
                            break;
                        case 14:
                            f.oPos += 2;
                            f.bPos += 2;
                            break;
                        case 15:
                            f.oPos += 4;
                            f.iiPos += 2;
                            f.ffPos += 1;
                            f.fPos += 1;
                            break;
                        case 16:
                            f.oPos += 4;
                            f.iPos += 1;
                            f.fPos += 2;
                            f.bPos += 1;
                            break;
                        case 17:
                            f.fPos += 2;
                            break;
                        case 18:
                            f.fPos += 2;
                            break;
                        case 19:
                            f.oPos += 1;
                            break;
                        case 20:
                            f.oPos += 1
                    }
                    f.prev = da.DRAW_ELLIPSE;
                    p = f;
                    z.fillCommands.drawEllipse(p.buffer.f[p.fPos],
                        p.buffer.f[p.fPos + 1], p.buffer.f[p.fPos + 2], p.buffer.f[p.fPos + 3]);
                    k && z.strokeCommands.drawEllipse(p.buffer.f[p.fPos], p.buffer.f[p.fPos + 1], p.buffer.f[p.fPos + 2], p.buffer.f[p.fPos + 3]);
                    break;
                case 8:
                    switch (f.prev._hx_index) {
                        case 0:
                            f.oPos += 2;
                            f.bPos += 2;
                            break;
                        case 1:
                            f.iPos += 1;
                            f.fPos += 1;
                            break;
                        case 2:
                            f.oPos += 4;
                            f.iiPos += 2;
                            f.ffPos += 1;
                            f.fPos += 1;
                            break;
                        case 3:
                            f.oPos += 1;
                            break;
                        case 4:
                            f.fPos += 6;
                            break;
                        case 5:
                            f.fPos += 4;
                            break;
                        case 6:
                            f.fPos += 3;
                            break;
                        case 7:
                            f.fPos += 4;
                            break;
                        case 8:
                            f.oPos += 3;
                            break;
                        case 9:
                            f.fPos += 4;
                            break;
                        case 10:
                            f.fPos +=
                                5;
                            f.oPos += 1;
                            break;
                        case 12:
                            f.oPos += 4;
                            break;
                        case 14:
                            f.oPos += 2;
                            f.bPos += 2;
                            break;
                        case 15:
                            f.oPos += 4;
                            f.iiPos += 2;
                            f.ffPos += 1;
                            f.fPos += 1;
                            break;
                        case 16:
                            f.oPos += 4;
                            f.iPos += 1;
                            f.fPos += 2;
                            f.bPos += 1;
                            break;
                        case 17:
                            f.fPos += 2;
                            break;
                        case 18:
                            f.fPos += 2;
                            break;
                        case 19:
                            f.oPos += 1;
                            break;
                        case 20:
                            f.oPos += 1
                    }
                    f.prev = da.DRAW_QUADS;
                    p = f;
                    z.fillCommands.drawQuads(p.buffer.o[p.oPos], p.buffer.o[p.oPos + 1], p.buffer.o[p.oPos + 2]);
                    break;
                case 9:
                    switch (f.prev._hx_index) {
                        case 0:
                            f.oPos += 2;
                            f.bPos += 2;
                            break;
                        case 1:
                            f.iPos += 1;
                            f.fPos += 1;
                            break;
                        case 2:
                            f.oPos +=
                                4;
                            f.iiPos += 2;
                            f.ffPos += 1;
                            f.fPos += 1;
                            break;
                        case 3:
                            f.oPos += 1;
                            break;
                        case 4:
                            f.fPos += 6;
                            break;
                        case 5:
                            f.fPos += 4;
                            break;
                        case 6:
                            f.fPos += 3;
                            break;
                        case 7:
                            f.fPos += 4;
                            break;
                        case 8:
                            f.oPos += 3;
                            break;
                        case 9:
                            f.fPos += 4;
                            break;
                        case 10:
                            f.fPos += 5;
                            f.oPos += 1;
                            break;
                        case 12:
                            f.oPos += 4;
                            break;
                        case 14:
                            f.oPos += 2;
                            f.bPos += 2;
                            break;
                        case 15:
                            f.oPos += 4;
                            f.iiPos += 2;
                            f.ffPos += 1;
                            f.fPos += 1;
                            break;
                        case 16:
                            f.oPos += 4;
                            f.iPos += 1;
                            f.fPos += 2;
                            f.bPos += 1;
                            break;
                        case 17:
                            f.fPos += 2;
                            break;
                        case 18:
                            f.fPos += 2;
                            break;
                        case 19:
                            f.oPos += 1;
                            break;
                        case 20:
                            f.oPos += 1
                    }
                    f.prev = da.DRAW_RECT;
                    p = f;
                    z.fillCommands.drawRect(p.buffer.f[p.fPos], p.buffer.f[p.fPos + 1], p.buffer.f[p.fPos + 2], p.buffer.f[p.fPos + 3]);
                    k && z.strokeCommands.drawRect(p.buffer.f[p.fPos], p.buffer.f[p.fPos + 1], p.buffer.f[p.fPos + 2], p.buffer.f[p.fPos + 3]);
                    break;
                case 10:
                    switch (f.prev._hx_index) {
                        case 0:
                            f.oPos += 2;
                            f.bPos += 2;
                            break;
                        case 1:
                            f.iPos += 1;
                            f.fPos += 1;
                            break;
                        case 2:
                            f.oPos += 4;
                            f.iiPos += 2;
                            f.ffPos += 1;
                            f.fPos += 1;
                            break;
                        case 3:
                            f.oPos += 1;
                            break;
                        case 4:
                            f.fPos += 6;
                            break;
                        case 5:
                            f.fPos += 4;
                            break;
                        case 6:
                            f.fPos += 3;
                            break;
                        case 7:
                            f.fPos += 4;
                            break;
                        case 8:
                            f.oPos +=
                                3;
                            break;
                        case 9:
                            f.fPos += 4;
                            break;
                        case 10:
                            f.fPos += 5;
                            f.oPos += 1;
                            break;
                        case 12:
                            f.oPos += 4;
                            break;
                        case 14:
                            f.oPos += 2;
                            f.bPos += 2;
                            break;
                        case 15:
                            f.oPos += 4;
                            f.iiPos += 2;
                            f.ffPos += 1;
                            f.fPos += 1;
                            break;
                        case 16:
                            f.oPos += 4;
                            f.iPos += 1;
                            f.fPos += 2;
                            f.bPos += 1;
                            break;
                        case 17:
                            f.fPos += 2;
                            break;
                        case 18:
                            f.fPos += 2;
                            break;
                        case 19:
                            f.oPos += 1;
                            break;
                        case 20:
                            f.oPos += 1
                    }
                    f.prev = da.DRAW_ROUND_RECT;
                    p = f;
                    z.fillCommands.drawRoundRect(p.buffer.f[p.fPos], p.buffer.f[p.fPos + 1], p.buffer.f[p.fPos + 2], p.buffer.f[p.fPos + 3], p.buffer.f[p.fPos + 4], p.buffer.o[p.oPos]);
                    k && z.strokeCommands.drawRoundRect(p.buffer.f[p.fPos], p.buffer.f[p.fPos + 1], p.buffer.f[p.fPos + 2], p.buffer.f[p.fPos + 3], p.buffer.f[p.fPos + 4], p.buffer.o[p.oPos]);
                    break;
                case 12:
                    switch (f.prev._hx_index) {
                        case 0:
                            f.oPos += 2;
                            f.bPos += 2;
                            break;
                        case 1:
                            f.iPos += 1;
                            f.fPos += 1;
                            break;
                        case 2:
                            f.oPos += 4;
                            f.iiPos += 2;
                            f.ffPos += 1;
                            f.fPos += 1;
                            break;
                        case 3:
                            f.oPos += 1;
                            break;
                        case 4:
                            f.fPos += 6;
                            break;
                        case 5:
                            f.fPos += 4;
                            break;
                        case 6:
                            f.fPos += 3;
                            break;
                        case 7:
                            f.fPos += 4;
                            break;
                        case 8:
                            f.oPos += 3;
                            break;
                        case 9:
                            f.fPos += 4;
                            break;
                        case 10:
                            f.fPos += 5;
                            f.oPos +=
                                1;
                            break;
                        case 12:
                            f.oPos += 4;
                            break;
                        case 14:
                            f.oPos += 2;
                            f.bPos += 2;
                            break;
                        case 15:
                            f.oPos += 4;
                            f.iiPos += 2;
                            f.ffPos += 1;
                            f.fPos += 1;
                            break;
                        case 16:
                            f.oPos += 4;
                            f.iPos += 1;
                            f.fPos += 2;
                            f.bPos += 1;
                            break;
                        case 17:
                            f.fPos += 2;
                            break;
                        case 18:
                            f.fPos += 2;
                            break;
                        case 19:
                            f.oPos += 1;
                            break;
                        case 20:
                            f.oPos += 1
                    }
                    f.prev = da.DRAW_TRIANGLES;
                    p = f;
                    z.fillCommands.drawTriangles(p.buffer.o[p.oPos], p.buffer.o[p.oPos + 1], p.buffer.o[p.oPos + 2], p.buffer.o[p.oPos + 3]);
                    break;
                case 13:
                    switch (f.prev._hx_index) {
                        case 0:
                            f.oPos += 2;
                            f.bPos += 2;
                            break;
                        case 1:
                            f.iPos += 1;
                            f.fPos +=
                                1;
                            break;
                        case 2:
                            f.oPos += 4;
                            f.iiPos += 2;
                            f.ffPos += 1;
                            f.fPos += 1;
                            break;
                        case 3:
                            f.oPos += 1;
                            break;
                        case 4:
                            f.fPos += 6;
                            break;
                        case 5:
                            f.fPos += 4;
                            break;
                        case 6:
                            f.fPos += 3;
                            break;
                        case 7:
                            f.fPos += 4;
                            break;
                        case 8:
                            f.oPos += 3;
                            break;
                        case 9:
                            f.fPos += 4;
                            break;
                        case 10:
                            f.fPos += 5;
                            f.oPos += 1;
                            break;
                        case 12:
                            f.oPos += 4;
                            break;
                        case 14:
                            f.oPos += 2;
                            f.bPos += 2;
                            break;
                        case 15:
                            f.oPos += 4;
                            f.iiPos += 2;
                            f.ffPos += 1;
                            f.fPos += 1;
                            break;
                        case 16:
                            f.oPos += 4;
                            f.iPos += 1;
                            f.fPos += 2;
                            f.bPos += 1;
                            break;
                        case 17:
                            f.fPos += 2;
                            break;
                        case 18:
                            f.fPos += 2;
                            break;
                        case 19:
                            f.oPos += 1;
                            break;
                        case 20:
                            f.oPos +=
                                1
                    }
                    f.prev = da.END_FILL;
                    z.endFill();
                    z.endStroke();
                    k = z.hasFill = !1;
                    z.bitmapFill = null;
                    d = c = 0;
                    break;
                case 14:
                    switch (f.prev._hx_index) {
                        case 0:
                            f.oPos += 2;
                            f.bPos += 2;
                            break;
                        case 1:
                            f.iPos += 1;
                            f.fPos += 1;
                            break;
                        case 2:
                            f.oPos += 4;
                            f.iiPos += 2;
                            f.ffPos += 1;
                            f.fPos += 1;
                            break;
                        case 3:
                            f.oPos += 1;
                            break;
                        case 4:
                            f.fPos += 6;
                            break;
                        case 5:
                            f.fPos += 4;
                            break;
                        case 6:
                            f.fPos += 3;
                            break;
                        case 7:
                            f.fPos += 4;
                            break;
                        case 8:
                            f.oPos += 3;
                            break;
                        case 9:
                            f.fPos += 4;
                            break;
                        case 10:
                            f.fPos += 5;
                            f.oPos += 1;
                            break;
                        case 12:
                            f.oPos += 4;
                            break;
                        case 14:
                            f.oPos += 2;
                            f.bPos += 2;
                            break;
                        case 15:
                            f.oPos +=
                                4;
                            f.iiPos += 2;
                            f.ffPos += 1;
                            f.fPos += 1;
                            break;
                        case 16:
                            f.oPos += 4;
                            f.iPos += 1;
                            f.fPos += 2;
                            f.bPos += 1;
                            break;
                        case 17:
                            f.fPos += 2;
                            break;
                        case 18:
                            f.fPos += 2;
                            break;
                        case 19:
                            f.oPos += 1;
                            break;
                        case 20:
                            f.oPos += 1
                    }
                    f.prev = da.LINE_BITMAP_STYLE;
                    p = f;
                    k || 0 == c && 0 == d || (z.strokeCommands.moveTo(c, d), d = c = 0);
                    k = !0;
                    z.strokeCommands.lineBitmapStyle(p.buffer.o[p.oPos], p.buffer.o[p.oPos + 1], p.buffer.b[p.bPos], p.buffer.b[p.bPos + 1]);
                    break;
                case 15:
                    switch (f.prev._hx_index) {
                        case 0:
                            f.oPos += 2;
                            f.bPos += 2;
                            break;
                        case 1:
                            f.iPos += 1;
                            f.fPos += 1;
                            break;
                        case 2:
                            f.oPos +=
                                4;
                            f.iiPos += 2;
                            f.ffPos += 1;
                            f.fPos += 1;
                            break;
                        case 3:
                            f.oPos += 1;
                            break;
                        case 4:
                            f.fPos += 6;
                            break;
                        case 5:
                            f.fPos += 4;
                            break;
                        case 6:
                            f.fPos += 3;
                            break;
                        case 7:
                            f.fPos += 4;
                            break;
                        case 8:
                            f.oPos += 3;
                            break;
                        case 9:
                            f.fPos += 4;
                            break;
                        case 10:
                            f.fPos += 5;
                            f.oPos += 1;
                            break;
                        case 12:
                            f.oPos += 4;
                            break;
                        case 14:
                            f.oPos += 2;
                            f.bPos += 2;
                            break;
                        case 15:
                            f.oPos += 4;
                            f.iiPos += 2;
                            f.ffPos += 1;
                            f.fPos += 1;
                            break;
                        case 16:
                            f.oPos += 4;
                            f.iPos += 1;
                            f.fPos += 2;
                            f.bPos += 1;
                            break;
                        case 17:
                            f.fPos += 2;
                            break;
                        case 18:
                            f.fPos += 2;
                            break;
                        case 19:
                            f.oPos += 1;
                            break;
                        case 20:
                            f.oPos += 1
                    }
                    f.prev = da.LINE_GRADIENT_STYLE;
                    p = f;
                    k || 0 == c && 0 == d || (z.strokeCommands.moveTo(c, d), d = c = 0);
                    k = !0;
                    z.strokeCommands.lineGradientStyle(p.buffer.o[p.oPos], p.buffer.ii[p.iiPos], p.buffer.ff[p.ffPos], p.buffer.ii[p.iiPos + 1], p.buffer.o[p.oPos + 1], p.buffer.o[p.oPos + 2], p.buffer.o[p.oPos + 3], p.buffer.f[p.fPos]);
                    break;
                case 16:
                    switch (f.prev._hx_index) {
                        case 0:
                            f.oPos += 2;
                            f.bPos += 2;
                            break;
                        case 1:
                            f.iPos += 1;
                            f.fPos += 1;
                            break;
                        case 2:
                            f.oPos += 4;
                            f.iiPos += 2;
                            f.ffPos += 1;
                            f.fPos += 1;
                            break;
                        case 3:
                            f.oPos += 1;
                            break;
                        case 4:
                            f.fPos += 6;
                            break;
                        case 5:
                            f.fPos += 4;
                            break;
                        case 6:
                            f.fPos +=
                                3;
                            break;
                        case 7:
                            f.fPos += 4;
                            break;
                        case 8:
                            f.oPos += 3;
                            break;
                        case 9:
                            f.fPos += 4;
                            break;
                        case 10:
                            f.fPos += 5;
                            f.oPos += 1;
                            break;
                        case 12:
                            f.oPos += 4;
                            break;
                        case 14:
                            f.oPos += 2;
                            f.bPos += 2;
                            break;
                        case 15:
                            f.oPos += 4;
                            f.iiPos += 2;
                            f.ffPos += 1;
                            f.fPos += 1;
                            break;
                        case 16:
                            f.oPos += 4;
                            f.iPos += 1;
                            f.fPos += 2;
                            f.bPos += 1;
                            break;
                        case 17:
                            f.fPos += 2;
                            break;
                        case 18:
                            f.fPos += 2;
                            break;
                        case 19:
                            f.oPos += 1;
                            break;
                        case 20:
                            f.oPos += 1
                    }
                    f.prev = da.LINE_STYLE;
                    p = f;
                    k || null == p.buffer.o[p.oPos] || 0 == c && 0 == d || (z.strokeCommands.moveTo(c, d), d = c = 0);
                    k = null != p.buffer.o[p.oPos];
                    z.strokeCommands.lineStyle(p.buffer.o[p.oPos],
                        p.buffer.i[p.iPos], p.buffer.f[p.fPos], p.buffer.b[p.bPos], p.buffer.o[p.oPos + 1], p.buffer.o[p.oPos + 2], p.buffer.o[p.oPos + 3], p.buffer.f[p.fPos + 1]);
                    break;
                case 17:
                    switch (f.prev._hx_index) {
                        case 0:
                            f.oPos += 2;
                            f.bPos += 2;
                            break;
                        case 1:
                            f.iPos += 1;
                            f.fPos += 1;
                            break;
                        case 2:
                            f.oPos += 4;
                            f.iiPos += 2;
                            f.ffPos += 1;
                            f.fPos += 1;
                            break;
                        case 3:
                            f.oPos += 1;
                            break;
                        case 4:
                            f.fPos += 6;
                            break;
                        case 5:
                            f.fPos += 4;
                            break;
                        case 6:
                            f.fPos += 3;
                            break;
                        case 7:
                            f.fPos += 4;
                            break;
                        case 8:
                            f.oPos += 3;
                            break;
                        case 9:
                            f.fPos += 4;
                            break;
                        case 10:
                            f.fPos += 5;
                            f.oPos += 1;
                            break;
                        case 12:
                            f.oPos +=
                                4;
                            break;
                        case 14:
                            f.oPos += 2;
                            f.bPos += 2;
                            break;
                        case 15:
                            f.oPos += 4;
                            f.iiPos += 2;
                            f.ffPos += 1;
                            f.fPos += 1;
                            break;
                        case 16:
                            f.oPos += 4;
                            f.iPos += 1;
                            f.fPos += 2;
                            f.bPos += 1;
                            break;
                        case 17:
                            f.fPos += 2;
                            break;
                        case 18:
                            f.fPos += 2;
                            break;
                        case 19:
                            f.oPos += 1;
                            break;
                        case 20:
                            f.oPos += 1
                    }
                    f.prev = da.LINE_TO;
                    p = f;
                    z.fillCommands.lineTo(p.buffer.f[p.fPos], p.buffer.f[p.fPos + 1]);
                    k ? z.strokeCommands.lineTo(p.buffer.f[p.fPos], p.buffer.f[p.fPos + 1]) : (c = p.buffer.f[p.fPos], d = p.buffer.f[p.fPos + 1]);
                    break;
                case 18:
                    switch (f.prev._hx_index) {
                        case 0:
                            f.oPos += 2;
                            f.bPos +=
                                2;
                            break;
                        case 1:
                            f.iPos += 1;
                            f.fPos += 1;
                            break;
                        case 2:
                            f.oPos += 4;
                            f.iiPos += 2;
                            f.ffPos += 1;
                            f.fPos += 1;
                            break;
                        case 3:
                            f.oPos += 1;
                            break;
                        case 4:
                            f.fPos += 6;
                            break;
                        case 5:
                            f.fPos += 4;
                            break;
                        case 6:
                            f.fPos += 3;
                            break;
                        case 7:
                            f.fPos += 4;
                            break;
                        case 8:
                            f.oPos += 3;
                            break;
                        case 9:
                            f.fPos += 4;
                            break;
                        case 10:
                            f.fPos += 5;
                            f.oPos += 1;
                            break;
                        case 12:
                            f.oPos += 4;
                            break;
                        case 14:
                            f.oPos += 2;
                            f.bPos += 2;
                            break;
                        case 15:
                            f.oPos += 4;
                            f.iiPos += 2;
                            f.ffPos += 1;
                            f.fPos += 1;
                            break;
                        case 16:
                            f.oPos += 4;
                            f.iPos += 1;
                            f.fPos += 2;
                            f.bPos += 1;
                            break;
                        case 17:
                            f.fPos += 2;
                            break;
                        case 18:
                            f.fPos += 2;
                            break;
                        case 19:
                            f.oPos += 1;
                            break;
                        case 20:
                            f.oPos += 1
                    }
                    f.prev = da.MOVE_TO;
                    p = f;
                    z.fillCommands.moveTo(p.buffer.f[p.fPos], p.buffer.f[p.fPos + 1]);
                    k ? z.strokeCommands.moveTo(p.buffer.f[p.fPos], p.buffer.f[p.fPos + 1]) : (c = p.buffer.f[p.fPos], d = p.buffer.f[p.fPos + 1]);
                    break;
                case 19:
                    switch (f.prev._hx_index) {
                        case 0:
                            f.oPos += 2;
                            f.bPos += 2;
                            break;
                        case 1:
                            f.iPos += 1;
                            f.fPos += 1;
                            break;
                        case 2:
                            f.oPos += 4;
                            f.iiPos += 2;
                            f.ffPos += 1;
                            f.fPos += 1;
                            break;
                        case 3:
                            f.oPos += 1;
                            break;
                        case 4:
                            f.fPos += 6;
                            break;
                        case 5:
                            f.fPos += 4;
                            break;
                        case 6:
                            f.fPos += 3;
                            break;
                        case 7:
                            f.fPos +=
                                4;
                            break;
                        case 8:
                            f.oPos += 3;
                            break;
                        case 9:
                            f.fPos += 4;
                            break;
                        case 10:
                            f.fPos += 5;
                            f.oPos += 1;
                            break;
                        case 12:
                            f.oPos += 4;
                            break;
                        case 14:
                            f.oPos += 2;
                            f.bPos += 2;
                            break;
                        case 15:
                            f.oPos += 4;
                            f.iiPos += 2;
                            f.ffPos += 1;
                            f.fPos += 1;
                            break;
                        case 16:
                            f.oPos += 4;
                            f.iPos += 1;
                            f.fPos += 2;
                            f.bPos += 1;
                            break;
                        case 17:
                            f.fPos += 2;
                            break;
                        case 18:
                            f.fPos += 2;
                            break;
                        case 19:
                            f.oPos += 1;
                            break;
                        case 20:
                            f.oPos += 1
                    }
                    f.prev = da.OVERRIDE_BLEND_MODE;
                    p = f;
                    b.__setBlendModeContext(z.context, p.buffer.o[p.oPos]);
                    break;
                case 21:
                    switch (f.prev._hx_index) {
                        case 0:
                            f.oPos += 2;
                            f.bPos += 2;
                            break;
                        case 1:
                            f.iPos += 1;
                            f.fPos += 1;
                            break;
                        case 2:
                            f.oPos += 4;
                            f.iiPos += 2;
                            f.ffPos += 1;
                            f.fPos += 1;
                            break;
                        case 3:
                            f.oPos += 1;
                            break;
                        case 4:
                            f.fPos += 6;
                            break;
                        case 5:
                            f.fPos += 4;
                            break;
                        case 6:
                            f.fPos += 3;
                            break;
                        case 7:
                            f.fPos += 4;
                            break;
                        case 8:
                            f.oPos += 3;
                            break;
                        case 9:
                            f.fPos += 4;
                            break;
                        case 10:
                            f.fPos += 5;
                            f.oPos += 1;
                            break;
                        case 12:
                            f.oPos += 4;
                            break;
                        case 14:
                            f.oPos += 2;
                            f.bPos += 2;
                            break;
                        case 15:
                            f.oPos += 4;
                            f.iiPos += 2;
                            f.ffPos += 1;
                            f.fPos += 1;
                            break;
                        case 16:
                            f.oPos += 4;
                            f.iPos += 1;
                            f.fPos += 2;
                            f.bPos += 1;
                            break;
                        case 17:
                            f.fPos += 2;
                            break;
                        case 18:
                            f.fPos += 2;
                            break;
                        case 19:
                            f.oPos +=
                                1;
                            break;
                        case 20:
                            f.oPos += 1
                    }
                    f.prev = da.WINDING_EVEN_ODD;
                    z.fillCommands.windingEvenOdd();
                    z.windingRule = "evenodd";
                    break;
                case 22:
                    switch (f.prev._hx_index) {
                        case 0:
                            f.oPos += 2;
                            f.bPos += 2;
                            break;
                        case 1:
                            f.iPos += 1;
                            f.fPos += 1;
                            break;
                        case 2:
                            f.oPos += 4;
                            f.iiPos += 2;
                            f.ffPos += 1;
                            f.fPos += 1;
                            break;
                        case 3:
                            f.oPos += 1;
                            break;
                        case 4:
                            f.fPos += 6;
                            break;
                        case 5:
                            f.fPos += 4;
                            break;
                        case 6:
                            f.fPos += 3;
                            break;
                        case 7:
                            f.fPos += 4;
                            break;
                        case 8:
                            f.oPos += 3;
                            break;
                        case 9:
                            f.fPos += 4;
                            break;
                        case 10:
                            f.fPos += 5;
                            f.oPos += 1;
                            break;
                        case 12:
                            f.oPos += 4;
                            break;
                        case 14:
                            f.oPos +=
                                2;
                            f.bPos += 2;
                            break;
                        case 15:
                            f.oPos += 4;
                            f.iiPos += 2;
                            f.ffPos += 1;
                            f.fPos += 1;
                            break;
                        case 16:
                            f.oPos += 4;
                            f.iPos += 1;
                            f.fPos += 2;
                            f.bPos += 1;
                            break;
                        case 17:
                            f.fPos += 2;
                            break;
                        case 18:
                            f.fPos += 2;
                            break;
                        case 19:
                            f.oPos += 1;
                            break;
                        case 20:
                            f.oPos += 1
                    }
                    f.prev = da.WINDING_NON_ZERO;
                    z.fillCommands.windingNonZero();
                    z.windingRule = "nonzero";
                    break;
                default:
                    switch (f.prev._hx_index) {
                        case 0:
                            f.oPos += 2;
                            f.bPos += 2;
                            break;
                        case 1:
                            f.iPos += 1;
                            f.fPos += 1;
                            break;
                        case 2:
                            f.oPos += 4;
                            f.iiPos += 2;
                            f.ffPos += 1;
                            f.fPos += 1;
                            break;
                        case 3:
                            f.oPos += 1;
                            break;
                        case 4:
                            f.fPos += 6;
                            break;
                        case 5:
                            f.fPos += 4;
                            break;
                        case 6:
                            f.fPos += 3;
                            break;
                        case 7:
                            f.fPos += 4;
                            break;
                        case 8:
                            f.oPos += 3;
                            break;
                        case 9:
                            f.fPos += 4;
                            break;
                        case 10:
                            f.fPos += 5;
                            f.oPos += 1;
                            break;
                        case 12:
                            f.oPos += 4;
                            break;
                        case 14:
                            f.oPos += 2;
                            f.bPos += 2;
                            break;
                        case 15:
                            f.oPos += 4;
                            f.iiPos += 2;
                            f.ffPos += 1;
                            f.fPos += 1;
                            break;
                        case 16:
                            f.oPos += 4;
                            f.iPos += 1;
                            f.fPos += 2;
                            f.bPos += 1;
                            break;
                        case 17:
                            f.fPos += 2;
                            break;
                        case 18:
                            f.fPos += 2;
                            break;
                        case 19:
                            f.oPos += 1;
                            break;
                        case 20:
                            f.oPos += 1
                    }
                    f.prev = p
            }
            0 < z.fillCommands.get_length() && z.endFill();
            0 < z.strokeCommands.get_length() && z.endStroke();
            f.destroy();
            a.__bitmap = Fb.fromCanvas(a.__canvas)
        }
        a.__softwareDirty = !1;
        a.set___dirty(!1);
        z.graphics = null
    }
};
z.renderMask = function(a, b) {
    if (0 != a.__commands.get_length()) {
        z.context = b.context;
        b = new ue(a.__commands);
        var c = 0;
        for (a = a.__commands.types; c < a.length;) {
            var d = a[c];
            ++c;
            switch (d._hx_index) {
                case 4:
                    switch (b.prev._hx_index) {
                        case 0:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 1:
                            b.iPos += 1;
                            b.fPos += 1;
                            break;
                        case 2:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 3:
                            b.oPos += 1;
                            break;
                        case 4:
                            b.fPos += 6;
                            break;
                        case 5:
                            b.fPos +=
                                4;
                            break;
                        case 6:
                            b.fPos += 3;
                            break;
                        case 7:
                            b.fPos += 4;
                            break;
                        case 8:
                            b.oPos += 3;
                            break;
                        case 9:
                            b.fPos += 4;
                            break;
                        case 10:
                            b.fPos += 5;
                            b.oPos += 1;
                            break;
                        case 12:
                            b.oPos += 4;
                            break;
                        case 14:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 15:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 16:
                            b.oPos += 4;
                            b.iPos += 1;
                            b.fPos += 2;
                            b.bPos += 1;
                            break;
                        case 17:
                            b.fPos += 2;
                            break;
                        case 18:
                            b.fPos += 2;
                            break;
                        case 19:
                            b.oPos += 1;
                            break;
                        case 20:
                            b.oPos += 1
                    }
                    b.prev = da.CUBIC_CURVE_TO;
                    d = b;
                    z.context.bezierCurveTo(d.buffer.f[d.fPos] - 0, d.buffer.f[d.fPos + 1] - 0, d.buffer.f[d.fPos +
                        2] - 0, d.buffer.f[d.fPos + 3] - 0, d.buffer.f[d.fPos + 4] - 0, d.buffer.f[d.fPos + 5] - 0);
                    break;
                case 5:
                    switch (b.prev._hx_index) {
                        case 0:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 1:
                            b.iPos += 1;
                            b.fPos += 1;
                            break;
                        case 2:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 3:
                            b.oPos += 1;
                            break;
                        case 4:
                            b.fPos += 6;
                            break;
                        case 5:
                            b.fPos += 4;
                            break;
                        case 6:
                            b.fPos += 3;
                            break;
                        case 7:
                            b.fPos += 4;
                            break;
                        case 8:
                            b.oPos += 3;
                            break;
                        case 9:
                            b.fPos += 4;
                            break;
                        case 10:
                            b.fPos += 5;
                            b.oPos += 1;
                            break;
                        case 12:
                            b.oPos += 4;
                            break;
                        case 14:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 15:
                            b.oPos +=
                                4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 16:
                            b.oPos += 4;
                            b.iPos += 1;
                            b.fPos += 2;
                            b.bPos += 1;
                            break;
                        case 17:
                            b.fPos += 2;
                            break;
                        case 18:
                            b.fPos += 2;
                            break;
                        case 19:
                            b.oPos += 1;
                            break;
                        case 20:
                            b.oPos += 1
                    }
                    b.prev = da.CURVE_TO;
                    d = b;
                    z.context.quadraticCurveTo(d.buffer.f[d.fPos] - 0, d.buffer.f[d.fPos + 1] - 0, d.buffer.f[d.fPos + 2] - 0, d.buffer.f[d.fPos + 3] - 0);
                    break;
                case 6:
                    switch (b.prev._hx_index) {
                        case 0:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 1:
                            b.iPos += 1;
                            b.fPos += 1;
                            break;
                        case 2:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 3:
                            b.oPos += 1;
                            break;
                        case 4:
                            b.fPos += 6;
                            break;
                        case 5:
                            b.fPos += 4;
                            break;
                        case 6:
                            b.fPos += 3;
                            break;
                        case 7:
                            b.fPos += 4;
                            break;
                        case 8:
                            b.oPos += 3;
                            break;
                        case 9:
                            b.fPos += 4;
                            break;
                        case 10:
                            b.fPos += 5;
                            b.oPos += 1;
                            break;
                        case 12:
                            b.oPos += 4;
                            break;
                        case 14:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 15:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 16:
                            b.oPos += 4;
                            b.iPos += 1;
                            b.fPos += 2;
                            b.bPos += 1;
                            break;
                        case 17:
                            b.fPos += 2;
                            break;
                        case 18:
                            b.fPos += 2;
                            break;
                        case 19:
                            b.oPos += 1;
                            break;
                        case 20:
                            b.oPos += 1
                    }
                    b.prev = da.DRAW_CIRCLE;
                    d = b;
                    z.context.arc(d.buffer.f[d.fPos] - 0, d.buffer.f[d.fPos +
                        1] - 0, d.buffer.f[d.fPos + 2], 0, 2 * Math.PI, !0);
                    break;
                case 7:
                    switch (b.prev._hx_index) {
                        case 0:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 1:
                            b.iPos += 1;
                            b.fPos += 1;
                            break;
                        case 2:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 3:
                            b.oPos += 1;
                            break;
                        case 4:
                            b.fPos += 6;
                            break;
                        case 5:
                            b.fPos += 4;
                            break;
                        case 6:
                            b.fPos += 3;
                            break;
                        case 7:
                            b.fPos += 4;
                            break;
                        case 8:
                            b.oPos += 3;
                            break;
                        case 9:
                            b.fPos += 4;
                            break;
                        case 10:
                            b.fPos += 5;
                            b.oPos += 1;
                            break;
                        case 12:
                            b.oPos += 4;
                            break;
                        case 14:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 15:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos +=
                                1;
                            break;
                        case 16:
                            b.oPos += 4;
                            b.iPos += 1;
                            b.fPos += 2;
                            b.bPos += 1;
                            break;
                        case 17:
                            b.fPos += 2;
                            break;
                        case 18:
                            b.fPos += 2;
                            break;
                        case 19:
                            b.oPos += 1;
                            break;
                        case 20:
                            b.oPos += 1
                    }
                    b.prev = da.DRAW_ELLIPSE;
                    var f = b;
                    d = f.buffer.f[f.fPos];
                    var h = f.buffer.f[f.fPos + 1];
                    var k = f.buffer.f[f.fPos + 2];
                    var g = f.buffer.f[f.fPos + 3];
                    d -= 0;
                    h -= 0;
                    f = k / 2 * .5522848;
                    var p = g / 2 * .5522848;
                    var q = d + k;
                    var u = h + g;
                    k = d + k / 2;
                    g = h + g / 2;
                    z.context.moveTo(d, g);
                    z.context.bezierCurveTo(d, g - p, k - f, h, k, h);
                    z.context.bezierCurveTo(k + f, h, q, g - p, q, g);
                    z.context.bezierCurveTo(q, g + p, k +
                        f, u, k, u);
                    z.context.bezierCurveTo(k - f, u, d, g + p, d, g);
                    break;
                case 9:
                    switch (b.prev._hx_index) {
                        case 0:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 1:
                            b.iPos += 1;
                            b.fPos += 1;
                            break;
                        case 2:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 3:
                            b.oPos += 1;
                            break;
                        case 4:
                            b.fPos += 6;
                            break;
                        case 5:
                            b.fPos += 4;
                            break;
                        case 6:
                            b.fPos += 3;
                            break;
                        case 7:
                            b.fPos += 4;
                            break;
                        case 8:
                            b.oPos += 3;
                            break;
                        case 9:
                            b.fPos += 4;
                            break;
                        case 10:
                            b.fPos += 5;
                            b.oPos += 1;
                            break;
                        case 12:
                            b.oPos += 4;
                            break;
                        case 14:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 15:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 16:
                            b.oPos += 4;
                            b.iPos += 1;
                            b.fPos += 2;
                            b.bPos += 1;
                            break;
                        case 17:
                            b.fPos += 2;
                            break;
                        case 18:
                            b.fPos += 2;
                            break;
                        case 19:
                            b.oPos += 1;
                            break;
                        case 20:
                            b.oPos += 1
                    }
                    b.prev = da.DRAW_RECT;
                    d = b;
                    z.context.beginPath();
                    z.context.rect(d.buffer.f[d.fPos] - 0, d.buffer.f[d.fPos + 1] - 0, d.buffer.f[d.fPos + 2], d.buffer.f[d.fPos + 3]);
                    z.context.closePath();
                    break;
                case 10:
                    switch (b.prev._hx_index) {
                        case 0:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 1:
                            b.iPos += 1;
                            b.fPos += 1;
                            break;
                        case 2:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 3:
                            b.oPos +=
                                1;
                            break;
                        case 4:
                            b.fPos += 6;
                            break;
                        case 5:
                            b.fPos += 4;
                            break;
                        case 6:
                            b.fPos += 3;
                            break;
                        case 7:
                            b.fPos += 4;
                            break;
                        case 8:
                            b.oPos += 3;
                            break;
                        case 9:
                            b.fPos += 4;
                            break;
                        case 10:
                            b.fPos += 5;
                            b.oPos += 1;
                            break;
                        case 12:
                            b.oPos += 4;
                            break;
                        case 14:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 15:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 16:
                            b.oPos += 4;
                            b.iPos += 1;
                            b.fPos += 2;
                            b.bPos += 1;
                            break;
                        case 17:
                            b.fPos += 2;
                            break;
                        case 18:
                            b.fPos += 2;
                            break;
                        case 19:
                            b.oPos += 1;
                            break;
                        case 20:
                            b.oPos += 1
                    }
                    b.prev = da.DRAW_ROUND_RECT;
                    d = b;
                    z.drawRoundRect(d.buffer.f[d.fPos] -
                        0, d.buffer.f[d.fPos + 1] - 0, d.buffer.f[d.fPos + 2], d.buffer.f[d.fPos + 3], d.buffer.f[d.fPos + 4], d.buffer.o[d.oPos]);
                    break;
                case 17:
                    switch (b.prev._hx_index) {
                        case 0:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 1:
                            b.iPos += 1;
                            b.fPos += 1;
                            break;
                        case 2:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 3:
                            b.oPos += 1;
                            break;
                        case 4:
                            b.fPos += 6;
                            break;
                        case 5:
                            b.fPos += 4;
                            break;
                        case 6:
                            b.fPos += 3;
                            break;
                        case 7:
                            b.fPos += 4;
                            break;
                        case 8:
                            b.oPos += 3;
                            break;
                        case 9:
                            b.fPos += 4;
                            break;
                        case 10:
                            b.fPos += 5;
                            b.oPos += 1;
                            break;
                        case 12:
                            b.oPos += 4;
                            break;
                        case 14:
                            b.oPos +=
                                2;
                            b.bPos += 2;
                            break;
                        case 15:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 16:
                            b.oPos += 4;
                            b.iPos += 1;
                            b.fPos += 2;
                            b.bPos += 1;
                            break;
                        case 17:
                            b.fPos += 2;
                            break;
                        case 18:
                            b.fPos += 2;
                            break;
                        case 19:
                            b.oPos += 1;
                            break;
                        case 20:
                            b.oPos += 1
                    }
                    b.prev = da.LINE_TO;
                    d = b;
                    z.context.lineTo(d.buffer.f[d.fPos] - 0, d.buffer.f[d.fPos + 1] - 0);
                    break;
                case 18:
                    switch (b.prev._hx_index) {
                        case 0:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 1:
                            b.iPos += 1;
                            b.fPos += 1;
                            break;
                        case 2:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 3:
                            b.oPos += 1;
                            break;
                        case 4:
                            b.fPos += 6;
                            break;
                        case 5:
                            b.fPos += 4;
                            break;
                        case 6:
                            b.fPos += 3;
                            break;
                        case 7:
                            b.fPos += 4;
                            break;
                        case 8:
                            b.oPos += 3;
                            break;
                        case 9:
                            b.fPos += 4;
                            break;
                        case 10:
                            b.fPos += 5;
                            b.oPos += 1;
                            break;
                        case 12:
                            b.oPos += 4;
                            break;
                        case 14:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 15:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 16:
                            b.oPos += 4;
                            b.iPos += 1;
                            b.fPos += 2;
                            b.bPos += 1;
                            break;
                        case 17:
                            b.fPos += 2;
                            break;
                        case 18:
                            b.fPos += 2;
                            break;
                        case 19:
                            b.oPos += 1;
                            break;
                        case 20:
                            b.oPos += 1
                    }
                    b.prev = da.MOVE_TO;
                    d = b;
                    z.context.moveTo(d.buffer.f[d.fPos] - 0, d.buffer.f[d.fPos + 1] - 0);
                    break;
                default:
                    switch (b.prev._hx_index) {
                        case 0:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 1:
                            b.iPos += 1;
                            b.fPos += 1;
                            break;
                        case 2:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 3:
                            b.oPos += 1;
                            break;
                        case 4:
                            b.fPos += 6;
                            break;
                        case 5:
                            b.fPos += 4;
                            break;
                        case 6:
                            b.fPos += 3;
                            break;
                        case 7:
                            b.fPos += 4;
                            break;
                        case 8:
                            b.oPos += 3;
                            break;
                        case 9:
                            b.fPos += 4;
                            break;
                        case 10:
                            b.fPos += 5;
                            b.oPos += 1;
                            break;
                        case 12:
                            b.oPos += 4;
                            break;
                        case 14:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 15:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 16:
                            b.oPos += 4;
                            b.iPos += 1;
                            b.fPos += 2;
                            b.bPos += 1;
                            break;
                        case 17:
                            b.fPos += 2;
                            break;
                        case 18:
                            b.fPos += 2;
                            break;
                        case 19:
                            b.oPos += 1;
                            break;
                        case 20:
                            b.oPos += 1
                    }
                    b.prev = d
            }
        }
        b.destroy()
    }
};
z.setSmoothing = function(a) {
    z.allowSmoothing || (a = !1);
    z.context.imageSmoothingEnabled != a && (z.context.imageSmoothingEnabled = a)
};
var ej = function() {};
g["openfl.display._internal.CanvasSimpleButton"] = ej;
ej.__name__ = "openfl.display._internal.CanvasSimpleButton";
ej.renderDrawable = function(a, b) {
    !a.__renderable || 0 >= a.__worldAlpha || null == a.__currentState || (b.__pushMaskObject(a), b.__renderDrawable(a.__currentState),
        b.__popMaskObject(a), b.__renderEvent(a))
};
ej.renderDrawableMask = function(a, b) {
    b.__renderDrawableMask(a.__currentState)
};
var Q = function() {};
g["openfl.display._internal.CanvasTextField"] = Q;
Q.__name__ = "openfl.display._internal.CanvasTextField";
Q.renderDrawable = function(a, b) {
    b.__isDOM && !a.__renderedOnCanvasWhileOnDOM && (a.__renderedOnCanvasWhileOnDOM = !0, 1 == a.get_type() && a.replaceText(0, a.__text.length, a.__text), a.__isHTML && a.__updateText(La.parse(a.__text, a.get_multiline(), a.__styleSheet, a.__textFormat,
        a.__textEngine.textFormatRanges)), a.__dirty = !0, a.__layoutDirty = !0, a.__renderDirty || (a.__renderDirty = !0, a.__setParentRenderDirty()));
    if (null == a.get_mask() || 0 < a.get_mask().get_width() && 0 < a.get_mask().get_height())
        if (b.__updateCacheBitmap(a, a.__dirty), null == a.__cacheBitmap || a.__isCacheBitmapRender) {
            var c = a.__textEngine,
                d = !(c.background || c.border),
                f = d ? c.textBounds : c.bounds,
                h = a.__graphics,
                k = 0;
            if (a.__dirty) {
                a.__updateLayout();
                null == h.__bounds && (h.__bounds = new na);
                if (0 == a.get_text().length) {
                    var g = c.bounds.width -
                        4,
                        p = a.get_defaultTextFormat().align;
                    k = 3 == p ? 0 : 4 == p ? g : g / 2;
                    switch (p) {
                        case 0:
                            k += a.get_defaultTextFormat().leftMargin / 2;
                            k -= a.get_defaultTextFormat().rightMargin / 2;
                            k += a.get_defaultTextFormat().indent / 2;
                            k += a.get_defaultTextFormat().blockIndent / 2;
                            break;
                        case 2:
                            k += a.get_defaultTextFormat().leftMargin;
                            k += a.get_defaultTextFormat().indent;
                            k += a.get_defaultTextFormat().blockIndent;
                            break;
                        case 3:
                            k += a.get_defaultTextFormat().leftMargin;
                            k += a.get_defaultTextFormat().indent;
                            k += a.get_defaultTextFormat().blockIndent;
                            break;
                        case 4:
                            k -= a.get_defaultTextFormat().rightMargin
                    }
                    d && (f.y = c.bounds.y, f.x = k)
                }
                h.__bounds.copyFrom(f)
            }
            p = b.__pixelRatio;
            h.__update(b.__worldTransform, p);
            if (a.__dirty || h.__softwareDirty) {
                g = Math.round(h.__width * p);
                var q = Math.round(h.__height * p);
                if (!(null != c.text && "" != c.text || c.background || c.border || c.__hasFocus || 1 == c.type && c.selectable) || (0 >= c.width || 0 >= c.height) && 2 != c.autoSize) a.__graphics.__canvas = null, a.__graphics.__context = null, a.__graphics.__bitmap = null, a.__graphics.__softwareDirty = !1, a.__graphics.set___dirty(!1),
                    a.__dirty = !1;
                else {
                    null == a.__graphics.__canvas && (a.__graphics.__canvas = window.document.createElement("canvas"), a.__graphics.__context = a.__graphics.__canvas.getContext("2d"));
                    Q.context = h.__context;
                    h.__canvas.width = g;
                    h.__canvas.height = q;
                    b.__isDOM && (h.__canvas.style.width = Math.round(g / p) + "px", h.__canvas.style.height = Math.round(q / p) + "px");
                    var u = ua.__pool.get();
                    u.scale(p, p);
                    u.concat(h.__renderTransform);
                    Q.context.setTransform(u.a, u.b, u.c, u.d, u.tx, u.ty);
                    ua.__pool.release(u);
                    null == Q.clearRect && (Q.clearRect =
                        "undefined" !== typeof navigator && "undefined" !== typeof navigator.isCocoonJS);
                    Q.clearRect && Q.context.clearRect(0, 0, h.__canvas.width, h.__canvas.height);
                    if (null != c.text && "" != c.text || c.__hasFocus) {
                        g = c.text;
                        h.__context.imageSmoothingEnabled = !b.__allowSmoothing || 0 == c.antiAliasType && 400 == c.sharpness ? !1 : !0;
                        if (c.border || c.background) {
                            Q.context.rect(.5, .5, f.width - 1, f.height - 1);
                            if (c.background) {
                                var m = O.hex(c.backgroundColor & 16777215, 6);
                                Q.context.fillStyle = "#" + m;
                                Q.context.fill()
                            }
                            c.border && (Q.context.lineWidth =
                                1, m = O.hex(c.borderColor & 16777215, 6), Q.context.strokeStyle = "#" + m, Q.context.stroke())
                        }
                        Q.context.textBaseline = "alphabetic";
                        Q.context.textAlign = "start";
                        q = -a.get_scrollH();
                        var r = d = 0;
                        for (m = a.get_scrollV() - 1; r < m;) {
                            var l = r++;
                            d -= c.lineHeights.get(l)
                        }
                        var x;
                        for (k = c.layoutGroups.iterator(); k.hasNext();) {
                            var D = k.next();
                            if (!(D.lineIndex < a.get_scrollV() - 1)) {
                                if (D.lineIndex > c.get_bottomScrollV() - 1) break;
                                u = "#" + O.hex(D.format.color & 16777215, 6);
                                Q.context.font = Nb.getFont(D.format);
                                Q.context.fillStyle = u;
                                Q.context.fillText(g.substring(D.startIndex,
                                    D.endIndex), D.offsetX + q - f.x, D.offsetY + D.ascent + d - f.y);
                                if (-1 < a.__caretIndex && c.selectable)
                                    if (a.__selectionIndex == a.__caretIndex) {
                                        if (a.__showCursor && D.startIndex <= a.__caretIndex && D.endIndex >= a.__caretIndex) {
                                            r = x = 0;
                                            for (m = a.__caretIndex - D.startIndex; r < m;) {
                                                l = r++;
                                                if (D.positions.length <= l) break;
                                                x += D.positions[l]
                                            }
                                            r = 0;
                                            m = a.get_scrollV();
                                            for (l = D.lineIndex + 1; m < l;) {
                                                var w = m++;
                                                r += c.lineHeights.get(w - 1)
                                            }
                                            Q.context.beginPath();
                                            m = O.hex(D.format.color & 16777215, 6);
                                            Q.context.strokeStyle = "#" + m;
                                            Q.context.moveTo(D.offsetX +
                                                x - a.get_scrollH() - f.x, r + 2 - f.y);
                                            Q.context.lineWidth = 1;
                                            Q.context.lineTo(D.offsetX + x - a.get_scrollH() - f.x, r + Nb.getFormatHeight(a.get_defaultTextFormat()) - 1 - f.y);
                                            Q.context.stroke();
                                            Q.context.closePath()
                                        }
                                    } else if (D.startIndex <= a.__caretIndex && D.endIndex >= a.__caretIndex || D.startIndex <= a.__selectionIndex && D.endIndex >= a.__selectionIndex || D.startIndex > a.__caretIndex && D.endIndex < a.__selectionIndex || D.startIndex > a.__selectionIndex && D.endIndex < a.__caretIndex) x = Math.min(a.__selectionIndex, a.__caretIndex) | 0, r = Math.max(a.__selectionIndex,
                                    a.__caretIndex) | 0, D.startIndex > x && (x = D.startIndex), D.endIndex < r && (r = D.endIndex), l = a.getCharBoundaries(x), r >= D.endIndex ? (m = a.getCharBoundaries(D.endIndex - 1), null != m && (m.x += m.width + 2)) : m = a.getCharBoundaries(r), null != l && null != m && (Q.context.fillStyle = "#000000", Q.context.fillRect(l.x + q - f.x, l.y + d, m.x - l.x, D.height), Q.context.fillStyle = "#FFFFFF", Q.context.fillText(g.substring(x, r), q + l.x - f.x, D.offsetY + D.ascent + d));
                                D.format.underline && (Q.context.beginPath(), Q.context.strokeStyle = u, Q.context.lineWidth = 1, u = D.offsetX +
                                    q - f.x, x = Math.ceil(D.offsetY + d + D.ascent - f.y) + Math.floor(.185 * D.ascent) + .5, Q.context.moveTo(u, x), Q.context.lineTo(u + D.width, x), Q.context.stroke(), Q.context.closePath())
                            }
                        }
                    } else {
                        if (c.border || c.background) c.border ? Q.context.rect(.5, .5, f.width - 1, f.height - 1) : Q.context.rect(0, 0, f.width, f.height), c.background && (m = O.hex(c.backgroundColor & 16777215, 6), Q.context.fillStyle = "#" + m, Q.context.fill()), c.border && (Q.context.lineWidth = 1, Q.context.lineCap = "square", m = O.hex(c.borderColor & 16777215, 6), Q.context.strokeStyle =
                            "#" + m, Q.context.stroke());
                        if (-1 < a.__caretIndex && c.selectable && a.__showCursor) {
                            q = -a.get_scrollH() + (d ? 0 : k);
                            r = d = 0;
                            for (m = a.get_scrollV() - 1; r < m;) l = r++, d += c.lineHeights.get(l);
                            Q.context.beginPath();
                            m = O.hex(a.get_defaultTextFormat().color & 16777215, 6);
                            Q.context.strokeStyle = "#" + m;
                            Q.context.moveTo(q + 2.5, d + 2.5);
                            Q.context.lineWidth = 1;
                            Q.context.lineTo(q + 2.5, d + Nb.getFormatHeight(a.get_defaultTextFormat()) - 1);
                            Q.context.stroke();
                            Q.context.closePath()
                        }
                    }
                    h.__bitmap = Fb.fromCanvas(a.__graphics.__canvas);
                    h.__bitmapScale =
                        p;
                    h.__visible = !0;
                    a.__dirty = !1;
                    h.__softwareDirty = !1;
                    h.set___dirty(!1)
                }
            }
            d = !1;
            0 == a.__textEngine.antiAliasType && 1 == a.__textEngine.gridFitType && (d = b.context.imageSmoothingEnabled) && (b.context.imageSmoothingEnabled = !1);
            if ((null != a.opaqueBackground || null != a.__graphics) && a.__renderable && (f = b.__getAlpha(a.__worldAlpha), !(0 >= f) && (null != a.opaqueBackground && !a.__isCacheBitmapRender && 0 < a.get_width() && 0 < a.get_height() && (b.__setBlendMode(a.__worldBlendMode), b.__pushMaskObject(a), c = b.context, b.setTransform(a.__renderTransform,
                    c), u = a.opaqueBackground, c.fillStyle = "rgb(" + (u >>> 16 & 255) + "," + (u >>> 8 & 255) + "," + (u & 255) + ")", c.fillRect(0, 0, a.get_width(), a.get_height()), b.__popMaskObject(a)), null != a.__graphics && a.__renderable && (f = b.__getAlpha(a.__worldAlpha), !(0 >= f) && (h = a.__graphics, null != h && (z.render(h, b), g = h.__width, q = h.__height, k = h.__canvas, null != k && h.__visible && 1 <= g && 1 <= q && (D = h.__worldTransform, c = b.context, p = a.__scrollRect, w = a.__worldScale9Grid, null == p || 0 < p.width && 0 < p.height))))))) {
                b.__setBlendMode(a.__worldBlendMode);
                b.__pushMaskObject(a);
                c.globalAlpha = f;
                if (null != w && 0 == D.b && 0 == D.c) {
                    p = b.__pixelRatio;
                    u = ua.__pool.get();
                    u.translate(D.tx, D.ty);
                    b.setTransform(u, c);
                    ua.__pool.release(u);
                    f = h.__bounds;
                    r = h.__renderTransform.a / h.__bitmapScale;
                    var J = h.__renderTransform.d / h.__bitmapScale;
                    m = r * D.a;
                    l = J * D.d;
                    h = Math.max(1, Math.round(w.x * r));
                    D = Math.round(w.y * J);
                    u = Math.max(1, Math.round((f.get_right() - w.get_right()) * r));
                    x = Math.round((f.get_bottom() - w.get_bottom()) * J);
                    r = Math.round(w.width * r);
                    w = Math.round(w.height * J);
                    J = Math.round(h / p);
                    var y = Math.round(D / p),
                        C = Math.round(u / p);
                    p = Math.round(x / p);
                    m = f.width * m - J - C;
                    f = f.height * l - y - p;
                    b.applySmoothing(c, !1);
                    0 != r && 0 != w ? (c.drawImage(k, 0, 0, h, D, 0, 0, J, y), c.drawImage(k, h, 0, r, D, J, 0, m, y), c.drawImage(k, h + r, 0, u, D, J + m, 0, C, y), c.drawImage(k, 0, D, h, w, 0, y, J, f), c.drawImage(k, h, D, r, w, J, y, m, f), c.drawImage(k, h + r, D, u, w, J + m, y, C, f), c.drawImage(k, 0, D + w, h, x, 0, y + f, J, p), c.drawImage(k, h, D + w, r, x, J, y + f, m, p), c.drawImage(k, h + r, D + w, u, x, J + m, y + f, C, p)) : 0 == r && 0 != w ? (h = J + m + C, c.drawImage(k, 0, 0, g, D, 0, 0, h, y), c.drawImage(k, 0, D, g, w, 0, y, h, f), c.drawImage(k,
                        0, D + w, g, x, 0, y + f, h, p)) : 0 == w && 0 != r && (f = y + f + p, c.drawImage(k, 0, 0, h, q, 0, 0, J, f), c.drawImage(k, h, 0, r, q, J, 0, m, f), c.drawImage(k, h + r, 0, u, q, J + m, 0, C, f))
                } else b.setTransform(D, c), c.drawImage(k, 0, 0, g, q);
                b.__popMaskObject(a)
            }
            d && (b.context.imageSmoothingEnabled = !0)
        } else a = a.__cacheBitmap, a.__renderable && (f = b.__getAlpha(a.__worldAlpha), 0 < f && null != a.__bitmapData && a.__bitmapData.__isValid && a.__bitmapData.readable && (c = b.context, b.__setBlendMode(a.__worldBlendMode), b.__pushMaskObject(a, !1), Ka.convertToCanvas(a.__bitmapData.image),
            c.globalAlpha = f, p = a.__scrollRect, b.setTransform(a.__renderTransform, c), b.__allowSmoothing && a.smoothing || (c.imageSmoothingEnabled = !1), null == p ? c.drawImage(a.__bitmapData.image.get_src(), 0, 0, a.__bitmapData.image.width, a.__bitmapData.image.height) : c.drawImage(a.__bitmapData.image.get_src(), p.x, p.y, p.width, p.height, p.x, p.y, p.width, p.height), b.__allowSmoothing && a.smoothing || (c.imageSmoothingEnabled = !0), b.__popMaskObject(a, !1)))
};
Q.renderDrawableMask = function(a, b) {
    Uf.renderDrawableMask(a, b)
};
var Ze = function() {};
g["openfl.display._internal.CanvasTilemap"] = Ze;
Ze.__name__ = "openfl.display._internal.CanvasTilemap";
Ze.renderTileContainer = function(a, b, c, d, f, h, k, g, p, q, u, m) {
    var n = b.context,
        r = b.__roundPixels,
        l = ua.__pool.get(),
        D = a.__tiles,
        x, w = null,
        J = 0;
    for (a = a.__length; J < a;) {
        var z = J++;
        var P = D[z];
        l.setTo(1, 0, 0, 1, -P.get_originX(), -P.get_originY());
        l.concat(P.get_matrix());
        l.concat(c);
        r && (l.tx = Math.round(l.tx), l.ty = Math.round(l.ty));
        var y = null != P.get_tileset() ? P.get_tileset() : d;
        z = P.get_alpha() * k;
        if ((x = P.get_visible()) &&
            !(0 >= z))
            if (h || (z = 1), g && (w = null != P.__blendMode ? P.__blendMode : p), 0 < P.__length) Ze.renderTileContainer(P, b, l, y, f, h, z, g, w, q, u, m);
            else if (null != y) {
            x = P.get_id();
            if (-1 == x) {
                if (P = P.__rect, null == P || 0 >= P.width || 0 >= P.height) continue
            } else {
                P = y.__data[x];
                if (null == P) continue;
                m.setTo(P.x, P.y, P.width, P.height);
                P = m
            }
            y = y.__bitmapData;
            null != y && null != y.image && (y != q && (null == y.image.buffer.__srcImage && Ka.convertToCanvas(y.image), u = y.image.get_src(), q = y), n.globalAlpha = z, g && b.__setBlendMode(w), b.setTransform(l, n), n.drawImage(u,
                P.x, P.y, P.width, P.height, 0, 0, P.width, P.height))
        }
    }
    ua.__pool.release(l)
};
Ze.renderDrawable = function(a, b) {
    b.__updateCacheBitmap(a, !1);
    if (null == a.__cacheBitmap || a.__isCacheBitmapRender) {
        if (!(null == a.opaqueBackground && null == a.__graphics || !a.__renderable || (w = b.__getAlpha(a.__worldAlpha), 0 >= w))) {
            if (null != a.opaqueBackground && !a.__isCacheBitmapRender && 0 < a.get_width() && 0 < a.get_height()) {
                b.__setBlendMode(a.__worldBlendMode);
                b.__pushMaskObject(a);
                J = b.context;
                b.setTransform(a.__renderTransform, J);
                var c = a.opaqueBackground;
                J.fillStyle = "rgb(" + (c >>> 16 & 255) + "," + (c >>> 8 & 255) + "," + (c & 255) + ")";
                J.fillRect(0, 0, a.get_width(), a.get_height());
                b.__popMaskObject(a)
            }
            if (null != a.__graphics && a.__renderable && (w = b.__getAlpha(a.__worldAlpha), !(0 >= w))) {
                var d = a.__graphics;
                if (null != d) {
                    z.render(d, b);
                    var f = d.__width,
                        h = d.__height;
                    c = d.__canvas;
                    if (null != c && d.__visible && 1 <= f && 1 <= h) {
                        var k = d.__worldTransform;
                        J = b.context;
                        y = a.__scrollRect;
                        var g = a.__worldScale9Grid;
                        if (null == y || 0 < y.width && 0 < y.height) {
                            b.__setBlendMode(a.__worldBlendMode);
                            b.__pushMaskObject(a);
                            J.globalAlpha = w;
                            if (null != g && 0 == k.b && 0 == k.c) {
                                var p = b.__pixelRatio;
                                w = ua.__pool.get();
                                w.translate(k.tx, k.ty);
                                b.setTransform(w, J);
                                ua.__pool.release(w);
                                w = d.__bounds;
                                var q = d.__renderTransform.a / d.__bitmapScale,
                                    u = d.__renderTransform.d / d.__bitmapScale,
                                    m = q * k.a,
                                    r = u * k.d;
                                k = Math.max(1, Math.round(g.x * q));
                                d = Math.round(g.y * u);
                                y = Math.max(1, Math.round((w.get_right() - g.get_right()) * q));
                                var l = Math.round((w.get_bottom() - g.get_bottom()) * u);
                                q = Math.round(g.width * q);
                                g = Math.round(g.height * u);
                                u = Math.round(k / p);
                                var D = Math.round(d /
                                        p),
                                    x = Math.round(y / p);
                                p = Math.round(l / p);
                                m = w.width * m - u - x;
                                w = w.height * r - D - p;
                                b.applySmoothing(J, !1);
                                0 != q && 0 != g ? (J.drawImage(c, 0, 0, k, d, 0, 0, u, D), J.drawImage(c, k, 0, q, d, u, 0, m, D), J.drawImage(c, k + q, 0, y, d, u + m, 0, x, D), J.drawImage(c, 0, d, k, g, 0, D, u, w), J.drawImage(c, k, d, q, g, u, D, m, w), J.drawImage(c, k + q, d, y, g, u + m, D, x, w), J.drawImage(c, 0, d + g, k, l, 0, D + w, u, p), J.drawImage(c, k, d + g, q, l, u, D + w, m, p), J.drawImage(c, k + q, d + g, y, l, u + m, D + w, x, p)) : 0 == q && 0 != g ? (h = u + m + x, J.drawImage(c, 0, 0, f, d, 0, 0, h, D), J.drawImage(c, 0, d, f, g, 0, D, h, w), J.drawImage(c,
                                    0, d + g, f, l, 0, D + w, h, p)) : 0 == g && 0 != q && (f = D + w + p, J.drawImage(c, 0, 0, k, h, 0, 0, u, f), J.drawImage(c, k, 0, q, h, u, 0, m, f), J.drawImage(c, k + q, 0, y, h, u + m, 0, x, f))
                            } else b.setTransform(k, J), J.drawImage(c, 0, 0, f, h);
                            b.__popMaskObject(a)
                        }
                    }
                }
            }
        }
        a.__renderable && 0 != a.__group.__tiles.length && (w = b.__getAlpha(a.__worldAlpha), 0 >= w || (J = b.context, b.__setBlendMode(a.__worldBlendMode), b.__pushMaskObject(a), c = na.__pool.get(), c.setTo(0, 0, a.__width, a.__height), b.__pushMaskRect(c, a.__renderTransform), b.__allowSmoothing && a.smoothing || (J.imageSmoothingEnabled = !1), Ze.renderTileContainer(a.__group, b, a.__renderTransform, a.__tileset, b.__allowSmoothing && a.smoothing, a.tileAlphaEnabled, w, a.tileBlendModeEnabled, a.__worldBlendMode, null, null, c), b.__allowSmoothing && a.smoothing || (J.imageSmoothingEnabled = !0), b.__popMaskRect(), b.__popMaskObject(a), na.__pool.release(c)))
    } else if (c = a.__cacheBitmap, c.__renderable) {
        var w = b.__getAlpha(c.__worldAlpha);
        if (0 < w && null != c.__bitmapData && c.__bitmapData.__isValid && c.__bitmapData.readable) {
            var J = b.context;
            b.__setBlendMode(c.__worldBlendMode);
            b.__pushMaskObject(c, !1);
            Ka.convertToCanvas(c.__bitmapData.image);
            J.globalAlpha = w;
            var y = c.__scrollRect;
            b.setTransform(c.__renderTransform, J);
            b.__allowSmoothing && c.smoothing || (J.imageSmoothingEnabled = !1);
            null == y ? J.drawImage(c.__bitmapData.image.get_src(), 0, 0, c.__bitmapData.image.width, c.__bitmapData.image.height) : J.drawImage(c.__bitmapData.image.get_src(), y.x, y.y, y.width, y.height, y.x, y.y, y.width, y.height);
            b.__allowSmoothing && c.smoothing || (J.imageSmoothingEnabled = !0);
            b.__popMaskObject(c, !1)
        }
    }
    b.__renderEvent(a)
};
Ze.renderDrawableMask = function(a, b) {};
var Vf = function() {};
g["openfl.display._internal.CanvasVideo"] = Vf;
Vf.__name__ = "openfl.display._internal.CanvasVideo";
Vf.render = function(a, b) {
    if (a.__renderable && null != a.__stream) {
        var c = b.__getAlpha(a.__worldAlpha);
        if (!(0 >= c)) {
            var d = b.context;
            if (null != a.__stream.__video) {
                b.__setBlendMode(a.__worldBlendMode);
                b.__pushMaskObject(a);
                d.globalAlpha = c;
                c = a.__scrollRect;
                var f = a.smoothing;
                b.setTransform(a.__worldTransform, d);
                f || (d.imageSmoothingEnabled = !1);
                null == c ? d.drawImage(a.__stream.__video,
                    0, 0, a.get_width(), a.get_height()) : d.drawImage(a.__stream.__video, c.x, c.y, c.width, c.height, c.x, c.y, c.width, c.height);
                f || (d.imageSmoothingEnabled = !0);
                b.__popMaskObject(a)
            }
        }
    }
};
Vf.renderDrawable = function(a, b) {
    Vf.render(a, b);
    b.__renderEvent(a)
};
Vf.renderDrawableMask = function(a, b) {};
var Vd = function() {};
g["openfl.display._internal.Context3DBitmap"] = Vd;
Vd.__name__ = "openfl.display._internal.Context3DBitmap";
Vd.render = function(a, b) {
    if (a.__renderable && !(0 >= a.__worldAlpha) && null != a.__bitmapData && a.__bitmapData.__isValid) {
        var c =
            b.__context3D;
        b.__setBlendMode(a.__worldBlendMode);
        b.__pushMaskObject(a);
        var d = b.__initDisplayShader(a.__worldShader);
        b.setShader(d);
        b.applyBitmapData(a.__bitmapData, b.__allowSmoothing && (a.smoothing || b.__upscaled));
        b.applyMatrix(b.__getMatrix(a.__renderTransform, a.pixelSnapping));
        b.applyAlpha(a.__worldAlpha);
        b.applyColorTransform(a.__worldColorTransform);
        b.updateShader();
        var f = a.__bitmapData.getVertexBuffer(c);
        null != d.__position && c.setVertexBufferAt(d.__position.index, f, 0, 3);
        null != d.__textureCoord &&
            c.setVertexBufferAt(d.__textureCoord.index, f, 3, 2);
        d = a.__bitmapData.getIndexBuffer(c);
        c.drawTriangles(d);
        b.__clearShader();
        b.__popMaskObject(a)
    }
};
Vd.renderDrawable = function(a, b) {
    b.__updateCacheBitmap(a, !1);
    null != a.__bitmapData && null != a.__bitmapData.image && (a.__imageVersion = a.__bitmapData.image.version);
    if (null == a.__cacheBitmap || a.__isCacheBitmapRender) {
        if (!(null == a.opaqueBackground && null == a.__graphics || !a.__renderable || 0 >= a.__worldAlpha)) {
            if (null != a.opaqueBackground && !a.__isCacheBitmapRender && 0 < a.get_width() &&
                0 < a.get_height()) {
                b.__setBlendMode(a.__worldBlendMode);
                b.__pushMaskObject(a);
                var c = b.__context3D,
                    d = na.__pool.get();
                d.setTo(0, 0, a.get_width(), a.get_height());
                b.__pushMaskRect(d, a.__renderTransform);
                var f = a.opaqueBackground;
                c.clear((f >>> 16 & 255) / 255, (f >>> 8 & 255) / 255, (f & 255) / 255, 1, 0, 0, 1);
                b.__popMaskRect();
                b.__popMaskObject(a);
                na.__pool.release(d)
            }
            null != a.__graphics && Ee.render(a, b)
        }
        Vd.render(a, b)
    } else Vd.render(a.__cacheBitmap, b);
    b.__renderEvent(a)
};
Vd.renderDrawableMask = function(a, b) {
    Vd.renderMask(a, b)
};
Vd.renderMask = function(a, b) {
    if (null != a.__bitmapData && a.__bitmapData.__isValid) {
        var c = b.__context3D,
            d = b.__maskShader;
        b.setShader(d);
        b.applyBitmapData(Xf.opaqueBitmapData, !0);
        b.applyMatrix(b.__getMatrix(a.__renderTransform, a.pixelSnapping));
        b.updateShader();
        var f = a.__bitmapData.getVertexBuffer(c);
        null != d.__position && c.setVertexBufferAt(d.__position.index, f, 0, 3);
        null != d.__textureCoord && c.setVertexBufferAt(d.__textureCoord.index, f, 3, 2);
        a = a.__bitmapData.getIndexBuffer(c);
        c.drawTriangles(a);
        b.__clearShader()
    }
};
var sj = function() {};
g["openfl.display._internal.Context3DBitmapData"] = sj;
sj.__name__ = "openfl.display._internal.Context3DBitmapData";
sj.renderDrawable = function(a, b) {
    var c = b.__context3D;
    b.__setBlendMode(10);
    var d = b.__defaultDisplayShader;
    b.setShader(d);
    b.applyBitmapData(a, b.__upscaled);
    b.applyMatrix(b.__getMatrix(a.__worldTransform, 1));
    b.applyAlpha(a.__worldAlpha);
    b.applyColorTransform(a.__worldColorTransform);
    b.updateShader();
    var f = a.getVertexBuffer(c);
    null != d.__position && c.setVertexBufferAt(d.__position.index,
        f, 0, 3);
    null != d.__textureCoord && c.setVertexBufferAt(d.__textureCoord.index, f, 3, 2);
    a = a.getIndexBuffer(c);
    c.drawTriangles(a);
    b.__clearShader()
};
sj.renderDrawableMask = function(a, b) {
    var c = b.__context3D,
        d = b.__maskShader;
    b.setShader(d);
    b.applyBitmapData(a, b.__upscaled);
    b.applyMatrix(b.__getMatrix(a.__worldTransform, 1));
    b.updateShader();
    var f = a.getVertexBuffer(c);
    null != d.__position && c.setVertexBufferAt(d.__position.index, f, 0, 3);
    null != d.__textureCoord && c.setVertexBufferAt(d.__textureCoord.index, f, 3, 2);
    a = a.getIndexBuffer(c);
    c.drawTriangles(a);
    b.__clearShader()
};
var Dh = function(a, b, c, d) {
    this.context3D = a;
    this.elementType = b;
    this.dataPerVertex = d;
    this.vertexCount = this.indexCount = 0;
    this.resize(c)
};
g["openfl.display._internal.Context3DBuffer"] = Dh;
Dh.__name__ = "openfl.display._internal.Context3DBuffer";
Dh.prototype = {
    flushVertexBufferData: function() {
        this.vertexBufferData.length > this.vertexCount && (this.vertexCount = this.vertexBufferData.length, this.vertexBuffer = this.context3D.createVertexBuffer(this.vertexCount, this.dataPerVertex,
            0));
        this.vertexBuffer.uploadFromTypedArray(ih.toArrayBufferView(this.vertexBufferData))
    },
    resize: function(a, b) {
        null == b && (b = -1);
        this.elementCount = a; - 1 == b && (b = this.dataPerVertex);
        b != this.dataPerVertex && (this.vertexBuffer = null, this.vertexCount = 0, this.dataPerVertex = b);
        var c = 0;
        switch (this.elementType._hx_index) {
            case 0:
                c = 4 * a;
                break;
            case 1:
                c = 3 * a;
                break;
            case 2:
                c = 3 * a
        }
        b *= c;
        if (null == this.vertexBufferData) {
            var d = c = null,
                f = null,
                h = null,
                k = null;
            this.vertexBufferData = b = null != b ? new Float32Array(b) : null != c ? new Float32Array(c) :
                null != d ? new Float32Array(d.__array) : null != f ? new Float32Array(f) : null != h ? null == k ? new Float32Array(h, 0) : new Float32Array(h, 0, k) : null
        } else b > this.vertexBufferData.length && (a = this.vertexBufferData, k = h = f = d = c = null, this.vertexBufferData = b = null != b ? new Float32Array(b) : null != c ? new Float32Array(c) : null != d ? new Float32Array(d.__array) : null != f ? new Float32Array(f) : null != h ? null == k ? new Float32Array(h, 0) : new Float32Array(h, 0, k) : null, this.vertexBufferData.set(a))
    },
    __class__: Dh
};
var yj = y["openfl.display._internal.Context3DElementType"] = {
    __ename__: "openfl.display._internal.Context3DElementType",
    __constructs__: null,
    QUADS: {
        _hx_name: "QUADS",
        _hx_index: 0,
        __enum__: "openfl.display._internal.Context3DElementType",
        toString: r
    },
    TRIANGLES: {
        _hx_name: "TRIANGLES",
        _hx_index: 1,
        __enum__: "openfl.display._internal.Context3DElementType",
        toString: r
    },
    TRIANGLE_INDICES: {
        _hx_name: "TRIANGLE_INDICES",
        _hx_index: 2,
        __enum__: "openfl.display._internal.Context3DElementType",
        toString: r
    }
};
yj.__constructs__ = [yj.QUADS, yj.TRIANGLES, yj.TRIANGLE_INDICES];
var Yf = function() {};
g["openfl.display._internal.Context3DDisplayObject"] = Yf;
Yf.__name__ = "openfl.display._internal.Context3DDisplayObject";
Yf.renderDrawable = function(a, b) {
    b.__updateCacheBitmap(a, !1);
    if (null != a.__cacheBitmap && !a.__isCacheBitmapRender) Vd.render(a.__cacheBitmap, b);
    else if (!(null == a.opaqueBackground && null == a.__graphics || !a.__renderable || 0 >= a.__worldAlpha)) {
        if (null != a.opaqueBackground && !a.__isCacheBitmapRender && 0 < a.get_width() && 0 < a.get_height()) {
            b.__setBlendMode(a.__worldBlendMode);
            b.__pushMaskObject(a);
            var c =
                b.__context3D,
                d = na.__pool.get();
            d.setTo(0, 0, a.get_width(), a.get_height());
            b.__pushMaskRect(d, a.__renderTransform);
            var f = a.opaqueBackground;
            c.clear((f >>> 16 & 255) / 255, (f >>> 8 & 255) / 255, (f & 255) / 255, 1, 0, 0, 1);
            b.__popMaskRect();
            b.__popMaskObject(a);
            na.__pool.release(d)
        }
        null != a.__graphics && Ee.render(a, b)
    }
    b.__renderEvent(a)
};
Yf.renderDrawableMask = function(a, b) {
    null != a.__graphics && Ee.renderMask(a, b)
};
var tj = function() {};
g["openfl.display._internal.Context3DDisplayObjectContainer"] = tj;
tj.__name__ = "openfl.display._internal.Context3DDisplayObjectContainer";
tj.renderDrawable = function(a, b) {
    for (var c = a.__removedChildren.iterator(); c.hasNext();) {
        var d = c.next();
        null == d.stage && d.__cleanup()
    }
    a.__removedChildren.set_length(0);
    if (a.__renderable && !(0 >= a.__worldAlpha) && (Yf.renderDrawable(a, b), null == a.__cacheBitmap || a.__isCacheBitmapRender)) {
        if (0 < a.__children.length)
            if (b.__pushMaskObject(a), null != b.__stage) {
                c = 0;
                for (d = a.__children; c < d.length;) {
                    var f = d[c];
                    ++c;
                    b.__renderDrawable(f);
                    f.__renderDirty = !1
                }
                a.__renderDirty = !1
            } else
                for (c = 0, d = a.__children; c < d.length;) f = d[c],
                    ++c, b.__renderDrawable(f);
        0 < a.__children.length && b.__popMaskObject(a)
    }
};
tj.renderDrawableMask = function(a, b) {
    for (var c = a.__removedChildren.iterator(); c.hasNext();) {
        var d = c.next();
        null == d.stage && d.__cleanup()
    }
    a.__removedChildren.set_length(0);
    null != a.__graphics && Ee.renderMask(a, b);
    c = 0;
    for (a = a.__children; c < a.length;) d = a[c], ++c, b.__renderDrawableMask(d)
};
var Yb = function() {};
g["openfl.display._internal.Context3DGraphics"] = Yb;
Yb.__name__ = "openfl.display._internal.Context3DGraphics";
Yb.buildBuffer = function(a,
    b) {
    var c, d = c = 0,
        f = 0,
        h = new ue(a.__commands);
    b = b.__context3D;
    for (var k = na.__pool.get(), g = ua.__pool.get(), p = null, q = 0, u = a.__commands.types; q < u.length;) {
        var m = u[q];
        ++q;
        switch (m._hx_index) {
            case 0:
                switch (h.prev._hx_index) {
                    case 0:
                        h.oPos += 2;
                        h.bPos += 2;
                        break;
                    case 1:
                        h.iPos += 1;
                        h.fPos += 1;
                        break;
                    case 2:
                        h.oPos += 4;
                        h.iiPos += 2;
                        h.ffPos += 1;
                        h.fPos += 1;
                        break;
                    case 3:
                        h.oPos += 1;
                        break;
                    case 4:
                        h.fPos += 6;
                        break;
                    case 5:
                        h.fPos += 4;
                        break;
                    case 6:
                        h.fPos += 3;
                        break;
                    case 7:
                        h.fPos += 4;
                        break;
                    case 8:
                        h.oPos += 3;
                        break;
                    case 9:
                        h.fPos += 4;
                        break;
                    case 10:
                        h.fPos +=
                            5;
                        h.oPos += 1;
                        break;
                    case 12:
                        h.oPos += 4;
                        break;
                    case 14:
                        h.oPos += 2;
                        h.bPos += 2;
                        break;
                    case 15:
                        h.oPos += 4;
                        h.iiPos += 2;
                        h.ffPos += 1;
                        h.fPos += 1;
                        break;
                    case 16:
                        h.oPos += 4;
                        h.iPos += 1;
                        h.fPos += 2;
                        h.bPos += 1;
                        break;
                    case 17:
                        h.fPos += 2;
                        break;
                    case 18:
                        h.fPos += 2;
                        break;
                    case 19:
                        h.oPos += 1;
                        break;
                    case 20:
                        h.oPos += 1
                }
                h.prev = da.BEGIN_BITMAP_FILL;
                p = h;
                p = p.buffer.o[p.oPos];
                break;
            case 1:
                p = null;
                switch (h.prev._hx_index) {
                    case 0:
                        h.oPos += 2;
                        h.bPos += 2;
                        break;
                    case 1:
                        h.iPos += 1;
                        h.fPos += 1;
                        break;
                    case 2:
                        h.oPos += 4;
                        h.iiPos += 2;
                        h.ffPos += 1;
                        h.fPos += 1;
                        break;
                    case 3:
                        h.oPos +=
                            1;
                        break;
                    case 4:
                        h.fPos += 6;
                        break;
                    case 5:
                        h.fPos += 4;
                        break;
                    case 6:
                        h.fPos += 3;
                        break;
                    case 7:
                        h.fPos += 4;
                        break;
                    case 8:
                        h.oPos += 3;
                        break;
                    case 9:
                        h.fPos += 4;
                        break;
                    case 10:
                        h.fPos += 5;
                        h.oPos += 1;
                        break;
                    case 12:
                        h.oPos += 4;
                        break;
                    case 14:
                        h.oPos += 2;
                        h.bPos += 2;
                        break;
                    case 15:
                        h.oPos += 4;
                        h.iiPos += 2;
                        h.ffPos += 1;
                        h.fPos += 1;
                        break;
                    case 16:
                        h.oPos += 4;
                        h.iPos += 1;
                        h.fPos += 2;
                        h.bPos += 1;
                        break;
                    case 17:
                        h.fPos += 2;
                        break;
                    case 18:
                        h.fPos += 2;
                        break;
                    case 19:
                        h.oPos += 1;
                        break;
                    case 20:
                        h.oPos += 1
                }
                h.prev = m;
                break;
            case 3:
                switch (h.prev._hx_index) {
                    case 0:
                        h.oPos += 2;
                        h.bPos += 2;
                        break;
                    case 1:
                        h.iPos += 1;
                        h.fPos += 1;
                        break;
                    case 2:
                        h.oPos += 4;
                        h.iiPos += 2;
                        h.ffPos += 1;
                        h.fPos += 1;
                        break;
                    case 3:
                        h.oPos += 1;
                        break;
                    case 4:
                        h.fPos += 6;
                        break;
                    case 5:
                        h.fPos += 4;
                        break;
                    case 6:
                        h.fPos += 3;
                        break;
                    case 7:
                        h.fPos += 4;
                        break;
                    case 8:
                        h.oPos += 3;
                        break;
                    case 9:
                        h.fPos += 4;
                        break;
                    case 10:
                        h.fPos += 5;
                        h.oPos += 1;
                        break;
                    case 12:
                        h.oPos += 4;
                        break;
                    case 14:
                        h.oPos += 2;
                        h.bPos += 2;
                        break;
                    case 15:
                        h.oPos += 4;
                        h.iiPos += 2;
                        h.ffPos += 1;
                        h.fPos += 1;
                        break;
                    case 16:
                        h.oPos += 4;
                        h.iPos += 1;
                        h.fPos += 2;
                        h.bPos += 1;
                        break;
                    case 17:
                        h.fPos += 2;
                        break;
                    case 18:
                        h.fPos +=
                            2;
                        break;
                    case 19:
                        h.oPos += 1;
                        break;
                    case 20:
                        h.oPos += 1
                }
                h.prev = da.BEGIN_SHADER_FILL;
                p = h;
                m = p.buffer.o[p.oPos];
                p = null;
                if (null != m)
                    for (var r = 0, l = m.inputCount; r < l;) {
                        var D = r++;
                        if ("bitmap" == m.inputRefs[D].name) {
                            p = m.inputs[D];
                            break
                        }
                    }
                break;
            case 8:
                if (null != p) {
                    switch (h.prev._hx_index) {
                        case 0:
                            h.oPos += 2;
                            h.bPos += 2;
                            break;
                        case 1:
                            h.iPos += 1;
                            h.fPos += 1;
                            break;
                        case 2:
                            h.oPos += 4;
                            h.iiPos += 2;
                            h.ffPos += 1;
                            h.fPos += 1;
                            break;
                        case 3:
                            h.oPos += 1;
                            break;
                        case 4:
                            h.fPos += 6;
                            break;
                        case 5:
                            h.fPos += 4;
                            break;
                        case 6:
                            h.fPos += 3;
                            break;
                        case 7:
                            h.fPos += 4;
                            break;
                        case 8:
                            h.oPos += 3;
                            break;
                        case 9:
                            h.fPos += 4;
                            break;
                        case 10:
                            h.fPos += 5;
                            h.oPos += 1;
                            break;
                        case 12:
                            h.oPos += 4;
                            break;
                        case 14:
                            h.oPos += 2;
                            h.bPos += 2;
                            break;
                        case 15:
                            h.oPos += 4;
                            h.iiPos += 2;
                            h.ffPos += 1;
                            h.fPos += 1;
                            break;
                        case 16:
                            h.oPos += 4;
                            h.iPos += 1;
                            h.fPos += 2;
                            h.bPos += 1;
                            break;
                        case 17:
                            h.fPos += 2;
                            break;
                        case 18:
                            h.fPos += 2;
                            break;
                        case 19:
                            h.oPos += 1;
                            break;
                        case 20:
                            h.oPos += 1
                    }
                    h.prev = da.DRAW_QUADS;
                    l = h;
                    m = l.buffer.o[l.oPos];
                    r = l.buffer.o[l.oPos + 1];
                    l = l.buffer.o[l.oPos + 2];
                    D = null != r;
                    var x = !1,
                        w = !1,
                        J = D ? r.get_length() : Math.floor(m.get_length() / 4);
                    if (0 == J) return;
                    null != l && (l.get_length() >= 6 * J ? w = x = !0 : l.get_length() >= 4 * J ? x = !0 : l.get_length() >= 2 * J && (w = !0));
                    null == a.__quadBuffer ? a.__quadBuffer = new Dh(b, yj.QUADS, J, 4) : a.__quadBuffer.resize(c + J, 4);
                    for (var z, y, C, t, v, G, F, I, eb, K, H, O, lb, Y, Va = a.__quadBuffer.vertexBufferData, E = p.width, sa = p.height, W = 0, A = J; W < A;) t = W++, z = 16 * (c + t), y = D ? 4 * r.get(t) : 4 * t, 0 > y || (k.setTo(m.get(y), m.get(y + 1), m.get(y + 2), m.get(y + 3)), y = k.width, C = k.height, 0 >= y || 0 >= C || (x && w ? (t *= 6, g.setTo(l.get(t), l.get(t + 1), l.get(t + 2), l.get(t + 3), l.get(t + 4),
                            l.get(t + 5))) : x ? (t *= 4, g.setTo(l.get(t), l.get(t + 1), l.get(t + 2), l.get(t + 3), k.x, k.y)) : w ? (t *= 2, g.tx = l.get(t), g.ty = l.get(t + 1)) : (g.tx = k.x, g.ty = k.y), t = k.x / E, v = k.y / sa, G = k.get_right() / E, F = k.get_bottom() / sa, I = 0 * g.a + 0 * g.c + g.tx, eb = 0 * g.b + 0 * g.d + g.ty, K = y * g.a + 0 * g.c + g.tx, H = y * g.b + 0 * g.d + g.ty, O = 0 * g.a + C * g.c + g.tx, lb = 0 * g.b + C * g.d + g.ty, Y = y * g.a + C * g.c + g.tx, y = y * g.b + C * g.d + g.ty, Va[z] = I, Va[z + 1] = eb, Va[z + 2] = t, Va[z + 3] = v, Va[z + 4] = K, Va[z + 4 + 1] = H, Va[z + 4 + 2] = G, Va[z + 4 + 3] = v, Va[z + 8] = O, Va[z + 8 + 1] = lb, Va[z + 8 + 2] = t, Va[z + 8 + 3] = F, Va[z + 12] = Y, Va[z + 12 + 1] =
                        y, Va[z + 12 + 2] = G, Va[z + 12 + 3] = F));
                    c += J
                }
                break;
            case 12:
                switch (h.prev._hx_index) {
                    case 0:
                        h.oPos += 2;
                        h.bPos += 2;
                        break;
                    case 1:
                        h.iPos += 1;
                        h.fPos += 1;
                        break;
                    case 2:
                        h.oPos += 4;
                        h.iiPos += 2;
                        h.ffPos += 1;
                        h.fPos += 1;
                        break;
                    case 3:
                        h.oPos += 1;
                        break;
                    case 4:
                        h.fPos += 6;
                        break;
                    case 5:
                        h.fPos += 4;
                        break;
                    case 6:
                        h.fPos += 3;
                        break;
                    case 7:
                        h.fPos += 4;
                        break;
                    case 8:
                        h.oPos += 3;
                        break;
                    case 9:
                        h.fPos += 4;
                        break;
                    case 10:
                        h.fPos += 5;
                        h.oPos += 1;
                        break;
                    case 12:
                        h.oPos += 4;
                        break;
                    case 14:
                        h.oPos += 2;
                        h.bPos += 2;
                        break;
                    case 15:
                        h.oPos += 4;
                        h.iiPos += 2;
                        h.ffPos += 1;
                        h.fPos += 1;
                        break;
                    case 16:
                        h.oPos += 4;
                        h.iPos += 1;
                        h.fPos += 2;
                        h.bPos += 1;
                        break;
                    case 17:
                        h.fPos += 2;
                        break;
                    case 18:
                        h.fPos += 2;
                        break;
                    case 19:
                        h.oPos += 1;
                        break;
                    case 20:
                        h.oPos += 1
                }
                h.prev = da.DRAW_TRIANGLES;
                l = h;
                m = l.buffer.o[l.oPos];
                r = l.buffer.o[l.oPos + 1];
                l = l.buffer.o[l.oPos + 2];
                D = null != r;
                J = Math.floor(m.get_length() / 2);
                x = D ? r.get_length() : J;
                z = (J = (w = null != l) && l.get_length() >= 3 * J) ? 4 : 2;
                Va = J ? 3 : 2;
                E = z + 2;
                sa = J ? f : d;
                Yb.resizeVertexBuffer(a, J, sa + x * E);
                W = J ? a.__vertexBufferDataUVT : a.__vertexBufferData;
                C = 0;
                for (t = x; C < t;) v = C++, A = sa + v * E, y = D ? 2 * r.get(v) : 2 *
                    v, v = D ? r.get(v) * Va : v * Va, J ? (G = l.get(v + 2), W[A] = m.get(y) / G, W[A + 1] = m.get(y + 1) / G, W[A + 2] = 0, W[A + 3] = 1 / G) : (W[A] = m.get(y), W[A + 1] = m.get(y + 1)), W[A + z] = w ? l.get(v) : 0, W[A + z + 1] = w ? l.get(v + 1) : 0;
                J ? f += x * E : d += x * E;
                break;
            case 13:
                p = null;
                break;
            default:
                switch (h.prev._hx_index) {
                    case 0:
                        h.oPos += 2;
                        h.bPos += 2;
                        break;
                    case 1:
                        h.iPos += 1;
                        h.fPos += 1;
                        break;
                    case 2:
                        h.oPos += 4;
                        h.iiPos += 2;
                        h.ffPos += 1;
                        h.fPos += 1;
                        break;
                    case 3:
                        h.oPos += 1;
                        break;
                    case 4:
                        h.fPos += 6;
                        break;
                    case 5:
                        h.fPos += 4;
                        break;
                    case 6:
                        h.fPos += 3;
                        break;
                    case 7:
                        h.fPos += 4;
                        break;
                    case 8:
                        h.oPos +=
                            3;
                        break;
                    case 9:
                        h.fPos += 4;
                        break;
                    case 10:
                        h.fPos += 5;
                        h.oPos += 1;
                        break;
                    case 12:
                        h.oPos += 4;
                        break;
                    case 14:
                        h.oPos += 2;
                        h.bPos += 2;
                        break;
                    case 15:
                        h.oPos += 4;
                        h.iiPos += 2;
                        h.ffPos += 1;
                        h.fPos += 1;
                        break;
                    case 16:
                        h.oPos += 4;
                        h.iPos += 1;
                        h.fPos += 2;
                        h.bPos += 1;
                        break;
                    case 17:
                        h.fPos += 2;
                        break;
                    case 18:
                        h.fPos += 2;
                        break;
                    case 19:
                        h.oPos += 1;
                        break;
                    case 20:
                        h.oPos += 1
                }
                h.prev = m
        }
    }
    0 < c && a.__quadBuffer.flushVertexBufferData();
    if (0 < d) {
        c = a.__vertexBuffer;
        if (null == c || d > a.__vertexBufferCount) c = b.createVertexBuffer(d, 4, 0), a.__vertexBuffer = c, a.__vertexBufferCount =
            d;
        c.uploadFromTypedArray(ih.toArrayBufferView(a.__vertexBufferData))
    }
    if (0 < f) {
        c = a.__vertexBufferUVT;
        if (null == c || f > a.__vertexBufferCountUVT) c = b.createVertexBuffer(f, 6, 0), a.__vertexBufferUVT = c, a.__vertexBufferCountUVT = f;
        c.uploadFromTypedArray(ih.toArrayBufferView(a.__vertexBufferDataUVT))
    }
    na.__pool.release(k);
    ua.__pool.release(g)
};
Yb.isCompatible = function(a) {
    if (null != a.__owner.__worldScale9Grid) return !1;
    var b = new ue(a.__commands),
        c = !1,
        d = !1,
        f = !1,
        h = 0;
    for (a = a.__commands.types; h < a.length;) {
        var k = a[h];
        ++h;
        switch (k._hx_index) {
            case 0:
                d = !0;
                f = c = !1;
                switch (b.prev._hx_index) {
                    case 0:
                        b.oPos += 2;
                        b.bPos += 2;
                        break;
                    case 1:
                        b.iPos += 1;
                        b.fPos += 1;
                        break;
                    case 2:
                        b.oPos += 4;
                        b.iiPos += 2;
                        b.ffPos += 1;
                        b.fPos += 1;
                        break;
                    case 3:
                        b.oPos += 1;
                        break;
                    case 4:
                        b.fPos += 6;
                        break;
                    case 5:
                        b.fPos += 4;
                        break;
                    case 6:
                        b.fPos += 3;
                        break;
                    case 7:
                        b.fPos += 4;
                        break;
                    case 8:
                        b.oPos += 3;
                        break;
                    case 9:
                        b.fPos += 4;
                        break;
                    case 10:
                        b.fPos += 5;
                        b.oPos += 1;
                        break;
                    case 12:
                        b.oPos += 4;
                        break;
                    case 14:
                        b.oPos += 2;
                        b.bPos += 2;
                        break;
                    case 15:
                        b.oPos += 4;
                        b.iiPos += 2;
                        b.ffPos += 1;
                        b.fPos += 1;
                        break;
                    case 16:
                        b.oPos +=
                            4;
                        b.iPos += 1;
                        b.fPos += 2;
                        b.bPos += 1;
                        break;
                    case 17:
                        b.fPos += 2;
                        break;
                    case 18:
                        b.fPos += 2;
                        break;
                    case 19:
                        b.oPos += 1;
                        break;
                    case 20:
                        b.oPos += 1
                }
                b.prev = k;
                break;
            case 1:
                d = !1;
                c = !0;
                f = !1;
                switch (b.prev._hx_index) {
                    case 0:
                        b.oPos += 2;
                        b.bPos += 2;
                        break;
                    case 1:
                        b.iPos += 1;
                        b.fPos += 1;
                        break;
                    case 2:
                        b.oPos += 4;
                        b.iiPos += 2;
                        b.ffPos += 1;
                        b.fPos += 1;
                        break;
                    case 3:
                        b.oPos += 1;
                        break;
                    case 4:
                        b.fPos += 6;
                        break;
                    case 5:
                        b.fPos += 4;
                        break;
                    case 6:
                        b.fPos += 3;
                        break;
                    case 7:
                        b.fPos += 4;
                        break;
                    case 8:
                        b.oPos += 3;
                        break;
                    case 9:
                        b.fPos += 4;
                        break;
                    case 10:
                        b.fPos += 5;
                        b.oPos += 1;
                        break;
                    case 12:
                        b.oPos += 4;
                        break;
                    case 14:
                        b.oPos += 2;
                        b.bPos += 2;
                        break;
                    case 15:
                        b.oPos += 4;
                        b.iiPos += 2;
                        b.ffPos += 1;
                        b.fPos += 1;
                        break;
                    case 16:
                        b.oPos += 4;
                        b.iPos += 1;
                        b.fPos += 2;
                        b.bPos += 1;
                        break;
                    case 17:
                        b.fPos += 2;
                        break;
                    case 18:
                        b.fPos += 2;
                        break;
                    case 19:
                        b.oPos += 1;
                        break;
                    case 20:
                        b.oPos += 1
                }
                b.prev = k;
                break;
            case 3:
                c = d = !1;
                f = !0;
                switch (b.prev._hx_index) {
                    case 0:
                        b.oPos += 2;
                        b.bPos += 2;
                        break;
                    case 1:
                        b.iPos += 1;
                        b.fPos += 1;
                        break;
                    case 2:
                        b.oPos += 4;
                        b.iiPos += 2;
                        b.ffPos += 1;
                        b.fPos += 1;
                        break;
                    case 3:
                        b.oPos += 1;
                        break;
                    case 4:
                        b.fPos += 6;
                        break;
                    case 5:
                        b.fPos += 4;
                        break;
                    case 6:
                        b.fPos += 3;
                        break;
                    case 7:
                        b.fPos += 4;
                        break;
                    case 8:
                        b.oPos += 3;
                        break;
                    case 9:
                        b.fPos += 4;
                        break;
                    case 10:
                        b.fPos += 5;
                        b.oPos += 1;
                        break;
                    case 12:
                        b.oPos += 4;
                        break;
                    case 14:
                        b.oPos += 2;
                        b.bPos += 2;
                        break;
                    case 15:
                        b.oPos += 4;
                        b.iiPos += 2;
                        b.ffPos += 1;
                        b.fPos += 1;
                        break;
                    case 16:
                        b.oPos += 4;
                        b.iPos += 1;
                        b.fPos += 2;
                        b.bPos += 1;
                        break;
                    case 17:
                        b.fPos += 2;
                        break;
                    case 18:
                        b.fPos += 2;
                        break;
                    case 19:
                        b.oPos += 1;
                        break;
                    case 20:
                        b.oPos += 1
                }
                b.prev = k;
                break;
            case 8:
                if (d || f) {
                    switch (b.prev._hx_index) {
                        case 0:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 1:
                            b.iPos += 1;
                            b.fPos +=
                                1;
                            break;
                        case 2:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 3:
                            b.oPos += 1;
                            break;
                        case 4:
                            b.fPos += 6;
                            break;
                        case 5:
                            b.fPos += 4;
                            break;
                        case 6:
                            b.fPos += 3;
                            break;
                        case 7:
                            b.fPos += 4;
                            break;
                        case 8:
                            b.oPos += 3;
                            break;
                        case 9:
                            b.fPos += 4;
                            break;
                        case 10:
                            b.fPos += 5;
                            b.oPos += 1;
                            break;
                        case 12:
                            b.oPos += 4;
                            break;
                        case 14:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 15:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 16:
                            b.oPos += 4;
                            b.iPos += 1;
                            b.fPos += 2;
                            b.bPos += 1;
                            break;
                        case 17:
                            b.fPos += 2;
                            break;
                        case 18:
                            b.fPos += 2;
                            break;
                        case 19:
                            b.oPos += 1;
                            break;
                        case 20:
                            b.oPos +=
                                1
                    }
                    b.prev = k
                } else return b.destroy(), !1;
                break;
            case 9:
                if (c) {
                    switch (b.prev._hx_index) {
                        case 0:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 1:
                            b.iPos += 1;
                            b.fPos += 1;
                            break;
                        case 2:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 3:
                            b.oPos += 1;
                            break;
                        case 4:
                            b.fPos += 6;
                            break;
                        case 5:
                            b.fPos += 4;
                            break;
                        case 6:
                            b.fPos += 3;
                            break;
                        case 7:
                            b.fPos += 4;
                            break;
                        case 8:
                            b.oPos += 3;
                            break;
                        case 9:
                            b.fPos += 4;
                            break;
                        case 10:
                            b.fPos += 5;
                            b.oPos += 1;
                            break;
                        case 12:
                            b.oPos += 4;
                            break;
                        case 14:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 15:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos +=
                                1;
                            break;
                        case 16:
                            b.oPos += 4;
                            b.iPos += 1;
                            b.fPos += 2;
                            b.bPos += 1;
                            break;
                        case 17:
                            b.fPos += 2;
                            break;
                        case 18:
                            b.fPos += 2;
                            break;
                        case 19:
                            b.oPos += 1;
                            break;
                        case 20:
                            b.oPos += 1
                    }
                    b.prev = k
                } else return b.destroy(), !1;
                break;
            case 12:
                if (d || f) {
                    switch (b.prev._hx_index) {
                        case 0:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 1:
                            b.iPos += 1;
                            b.fPos += 1;
                            break;
                        case 2:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 3:
                            b.oPos += 1;
                            break;
                        case 4:
                            b.fPos += 6;
                            break;
                        case 5:
                            b.fPos += 4;
                            break;
                        case 6:
                            b.fPos += 3;
                            break;
                        case 7:
                            b.fPos += 4;
                            break;
                        case 8:
                            b.oPos += 3;
                            break;
                        case 9:
                            b.fPos +=
                                4;
                            break;
                        case 10:
                            b.fPos += 5;
                            b.oPos += 1;
                            break;
                        case 12:
                            b.oPos += 4;
                            break;
                        case 14:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 15:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 16:
                            b.oPos += 4;
                            b.iPos += 1;
                            b.fPos += 2;
                            b.bPos += 1;
                            break;
                        case 17:
                            b.fPos += 2;
                            break;
                        case 18:
                            b.fPos += 2;
                            break;
                        case 19:
                            b.oPos += 1;
                            break;
                        case 20:
                            b.oPos += 1
                    }
                    b.prev = k
                } else return b.destroy(), !1;
                break;
            case 13:
                f = c = d = !1;
                switch (b.prev._hx_index) {
                    case 0:
                        b.oPos += 2;
                        b.bPos += 2;
                        break;
                    case 1:
                        b.iPos += 1;
                        b.fPos += 1;
                        break;
                    case 2:
                        b.oPos += 4;
                        b.iiPos += 2;
                        b.ffPos += 1;
                        b.fPos += 1;
                        break;
                    case 3:
                        b.oPos += 1;
                        break;
                    case 4:
                        b.fPos += 6;
                        break;
                    case 5:
                        b.fPos += 4;
                        break;
                    case 6:
                        b.fPos += 3;
                        break;
                    case 7:
                        b.fPos += 4;
                        break;
                    case 8:
                        b.oPos += 3;
                        break;
                    case 9:
                        b.fPos += 4;
                        break;
                    case 10:
                        b.fPos += 5;
                        b.oPos += 1;
                        break;
                    case 12:
                        b.oPos += 4;
                        break;
                    case 14:
                        b.oPos += 2;
                        b.bPos += 2;
                        break;
                    case 15:
                        b.oPos += 4;
                        b.iiPos += 2;
                        b.ffPos += 1;
                        b.fPos += 1;
                        break;
                    case 16:
                        b.oPos += 4;
                        b.iPos += 1;
                        b.fPos += 2;
                        b.bPos += 1;
                        break;
                    case 17:
                        b.fPos += 2;
                        break;
                    case 18:
                        b.fPos += 2;
                        break;
                    case 19:
                        b.oPos += 1;
                        break;
                    case 20:
                        b.oPos += 1
                }
                b.prev = k;
                break;
            case 18:
                switch (b.prev._hx_index) {
                    case 0:
                        b.oPos +=
                            2;
                        b.bPos += 2;
                        break;
                    case 1:
                        b.iPos += 1;
                        b.fPos += 1;
                        break;
                    case 2:
                        b.oPos += 4;
                        b.iiPos += 2;
                        b.ffPos += 1;
                        b.fPos += 1;
                        break;
                    case 3:
                        b.oPos += 1;
                        break;
                    case 4:
                        b.fPos += 6;
                        break;
                    case 5:
                        b.fPos += 4;
                        break;
                    case 6:
                        b.fPos += 3;
                        break;
                    case 7:
                        b.fPos += 4;
                        break;
                    case 8:
                        b.oPos += 3;
                        break;
                    case 9:
                        b.fPos += 4;
                        break;
                    case 10:
                        b.fPos += 5;
                        b.oPos += 1;
                        break;
                    case 12:
                        b.oPos += 4;
                        break;
                    case 14:
                        b.oPos += 2;
                        b.bPos += 2;
                        break;
                    case 15:
                        b.oPos += 4;
                        b.iiPos += 2;
                        b.ffPos += 1;
                        b.fPos += 1;
                        break;
                    case 16:
                        b.oPos += 4;
                        b.iPos += 1;
                        b.fPos += 2;
                        b.bPos += 1;
                        break;
                    case 17:
                        b.fPos += 2;
                        break;
                    case 18:
                        b.fPos +=
                            2;
                        break;
                    case 19:
                        b.oPos += 1;
                        break;
                    case 20:
                        b.oPos += 1
                }
                b.prev = k;
                break;
            case 19:
                switch (b.prev._hx_index) {
                    case 0:
                        b.oPos += 2;
                        b.bPos += 2;
                        break;
                    case 1:
                        b.iPos += 1;
                        b.fPos += 1;
                        break;
                    case 2:
                        b.oPos += 4;
                        b.iiPos += 2;
                        b.ffPos += 1;
                        b.fPos += 1;
                        break;
                    case 3:
                        b.oPos += 1;
                        break;
                    case 4:
                        b.fPos += 6;
                        break;
                    case 5:
                        b.fPos += 4;
                        break;
                    case 6:
                        b.fPos += 3;
                        break;
                    case 7:
                        b.fPos += 4;
                        break;
                    case 8:
                        b.oPos += 3;
                        break;
                    case 9:
                        b.fPos += 4;
                        break;
                    case 10:
                        b.fPos += 5;
                        b.oPos += 1;
                        break;
                    case 12:
                        b.oPos += 4;
                        break;
                    case 14:
                        b.oPos += 2;
                        b.bPos += 2;
                        break;
                    case 15:
                        b.oPos += 4;
                        b.iiPos += 2;
                        b.ffPos += 1;
                        b.fPos += 1;
                        break;
                    case 16:
                        b.oPos += 4;
                        b.iPos += 1;
                        b.fPos += 2;
                        b.bPos += 1;
                        break;
                    case 17:
                        b.fPos += 2;
                        break;
                    case 18:
                        b.fPos += 2;
                        break;
                    case 19:
                        b.oPos += 1;
                        break;
                    case 20:
                        b.oPos += 1
                }
                b.prev = k;
                break;
            default:
                return b.destroy(), !1
        }
    }
    b.destroy();
    return !0
};
Yb.render = function(a, b) {
    if (a.__visible && 0 != a.__commands.get_length())
        if (null != a.__bitmap && !a.__dirty || !Yb.isCompatible(a)) {
            b.__softwareRenderer.__pixelRatio = b.__pixelRatio;
            var c = b.__softwareRenderer.__worldTransform;
            b.__softwareRenderer.__worldTransform = 7 == a.__owner.__drawableType ?
                ua.__identity : b.__worldTransform;
            z.render(a, b.__softwareRenderer);
            b.__softwareRenderer.__worldTransform = c
        } else {
            a.__bitmap = null;
            a.__update(b.__worldTransform, b.__pixelRatio);
            var d = a.__width,
                f = a.__height;
            if (null != a.__bounds && 1 <= d && 1 <= f) {
                (a.__hardwareDirty || null == a.__quadBuffer && null == a.__vertexBuffer && null == a.__vertexBufferUVT) && Yb.buildBuffer(a, b);
                c = new ue(a.__commands);
                for (var h = b.__context3D, k = ua.__pool.get(), g = null, p = null, q = !1, u = !1, m = null, r = 0, l = 0, D = 0, x = 0, w = 0, J = a.__commands.types; w < J.length;) switch (d =
                    J[w], ++w, d._hx_index) {
                    case 0:
                        switch (c.prev._hx_index) {
                            case 0:
                                c.oPos += 2;
                                c.bPos += 2;
                                break;
                            case 1:
                                c.iPos += 1;
                                c.fPos += 1;
                                break;
                            case 2:
                                c.oPos += 4;
                                c.iiPos += 2;
                                c.ffPos += 1;
                                c.fPos += 1;
                                break;
                            case 3:
                                c.oPos += 1;
                                break;
                            case 4:
                                c.fPos += 6;
                                break;
                            case 5:
                                c.fPos += 4;
                                break;
                            case 6:
                                c.fPos += 3;
                                break;
                            case 7:
                                c.fPos += 4;
                                break;
                            case 8:
                                c.oPos += 3;
                                break;
                            case 9:
                                c.fPos += 4;
                                break;
                            case 10:
                                c.fPos += 5;
                                c.oPos += 1;
                                break;
                            case 12:
                                c.oPos += 4;
                                break;
                            case 14:
                                c.oPos += 2;
                                c.bPos += 2;
                                break;
                            case 15:
                                c.oPos += 4;
                                c.iiPos += 2;
                                c.ffPos += 1;
                                c.fPos += 1;
                                break;
                            case 16:
                                c.oPos += 4;
                                c.iPos += 1;
                                c.fPos += 2;
                                c.bPos += 1;
                                break;
                            case 17:
                                c.fPos += 2;
                                break;
                            case 18:
                                c.fPos += 2;
                                break;
                            case 19:
                                c.oPos += 1;
                                break;
                            case 20:
                                c.oPos += 1
                        }
                        c.prev = da.BEGIN_BITMAP_FILL;
                        u = c;
                        p = u.buffer.o[u.oPos];
                        q = u.buffer.b[u.bPos];
                        u = u.buffer.b[u.bPos + 1];
                        m = g = null;
                        break;
                    case 1:
                        switch (c.prev._hx_index) {
                            case 0:
                                c.oPos += 2;
                                c.bPos += 2;
                                break;
                            case 1:
                                c.iPos += 1;
                                c.fPos += 1;
                                break;
                            case 2:
                                c.oPos += 4;
                                c.iiPos += 2;
                                c.ffPos += 1;
                                c.fPos += 1;
                                break;
                            case 3:
                                c.oPos += 1;
                                break;
                            case 4:
                                c.fPos += 6;
                                break;
                            case 5:
                                c.fPos += 4;
                                break;
                            case 6:
                                c.fPos += 3;
                                break;
                            case 7:
                                c.fPos += 4;
                                break;
                            case 8:
                                c.oPos += 3;
                                break;
                            case 9:
                                c.fPos += 4;
                                break;
                            case 10:
                                c.fPos += 5;
                                c.oPos += 1;
                                break;
                            case 12:
                                c.oPos += 4;
                                break;
                            case 14:
                                c.oPos += 2;
                                c.bPos += 2;
                                break;
                            case 15:
                                c.oPos += 4;
                                c.iiPos += 2;
                                c.ffPos += 1;
                                c.fPos += 1;
                                break;
                            case 16:
                                c.oPos += 4;
                                c.iPos += 1;
                                c.fPos += 2;
                                c.bPos += 1;
                                break;
                            case 17:
                                c.fPos += 2;
                                break;
                            case 18:
                                c.fPos += 2;
                                break;
                            case 19:
                                c.oPos += 1;
                                break;
                            case 20:
                                c.oPos += 1
                        }
                        c.prev = da.BEGIN_FILL;
                        p = c;
                        m = (p.buffer.i[p.iPos] | 0) & 16777215 | (255 * p.buffer.f[p.fPos] | 0) << 24;
                        p = g = null;
                        break;
                    case 3:
                        switch (c.prev._hx_index) {
                            case 0:
                                c.oPos += 2;
                                c.bPos += 2;
                                break;
                            case 1:
                                c.iPos += 1;
                                c.fPos += 1;
                                break;
                            case 2:
                                c.oPos += 4;
                                c.iiPos += 2;
                                c.ffPos += 1;
                                c.fPos += 1;
                                break;
                            case 3:
                                c.oPos += 1;
                                break;
                            case 4:
                                c.fPos += 6;
                                break;
                            case 5:
                                c.fPos += 4;
                                break;
                            case 6:
                                c.fPos += 3;
                                break;
                            case 7:
                                c.fPos += 4;
                                break;
                            case 8:
                                c.oPos += 3;
                                break;
                            case 9:
                                c.fPos += 4;
                                break;
                            case 10:
                                c.fPos += 5;
                                c.oPos += 1;
                                break;
                            case 12:
                                c.oPos += 4;
                                break;
                            case 14:
                                c.oPos += 2;
                                c.bPos += 2;
                                break;
                            case 15:
                                c.oPos += 4;
                                c.iiPos += 2;
                                c.ffPos += 1;
                                c.fPos += 1;
                                break;
                            case 16:
                                c.oPos += 4;
                                c.iPos += 1;
                                c.fPos += 2;
                                c.bPos += 1;
                                break;
                            case 17:
                                c.fPos += 2;
                                break;
                            case 18:
                                c.fPos += 2;
                                break;
                            case 19:
                                c.oPos += 1;
                                break;
                            case 20:
                                c.oPos += 1
                        }
                        c.prev = da.BEGIN_SHADER_FILL;
                        l = c;
                        g = l.buffer.o[l.oPos];
                        l = 0;
                        p = null == g || null == g.shader || null == g.shader.__bitmap ? null : g.shader.__bitmap.input;
                        m = null;
                        break;
                    case 8:
                        if (null != p) {
                            switch (c.prev._hx_index) {
                                case 0:
                                    c.oPos += 2;
                                    c.bPos += 2;
                                    break;
                                case 1:
                                    c.iPos += 1;
                                    c.fPos += 1;
                                    break;
                                case 2:
                                    c.oPos += 4;
                                    c.iiPos += 2;
                                    c.ffPos += 1;
                                    c.fPos += 1;
                                    break;
                                case 3:
                                    c.oPos += 1;
                                    break;
                                case 4:
                                    c.fPos += 6;
                                    break;
                                case 5:
                                    c.fPos += 4;
                                    break;
                                case 6:
                                    c.fPos += 3;
                                    break;
                                case 7:
                                    c.fPos += 4;
                                    break;
                                case 8:
                                    c.oPos += 3;
                                    break;
                                case 9:
                                    c.fPos +=
                                        4;
                                    break;
                                case 10:
                                    c.fPos += 5;
                                    c.oPos += 1;
                                    break;
                                case 12:
                                    c.oPos += 4;
                                    break;
                                case 14:
                                    c.oPos += 2;
                                    c.bPos += 2;
                                    break;
                                case 15:
                                    c.oPos += 4;
                                    c.iiPos += 2;
                                    c.ffPos += 1;
                                    c.fPos += 1;
                                    break;
                                case 16:
                                    c.oPos += 4;
                                    c.iPos += 1;
                                    c.fPos += 2;
                                    c.bPos += 1;
                                    break;
                                case 17:
                                    c.fPos += 2;
                                    break;
                                case 18:
                                    c.fPos += 2;
                                    break;
                                case 19:
                                    c.oPos += 1;
                                    break;
                                case 20:
                                    c.oPos += 1
                            }
                            c.prev = da.DRAW_QUADS;
                            var y = c;
                            d = y.buffer.o[y.oPos];
                            y = y.buffer.o[y.oPos + 1];
                            d = null != y ? y.get_length() : Math.floor(d.get_length() / 4);
                            var C = b.__getMatrix(a.__owner.__renderTransform, 1);
                            null == g || Yb.maskRender ?
                                (y = Yb.maskRender ? b.__maskShader : b.__initGraphicsShader(null), b.setShader(y), b.applyMatrix(C), b.applyBitmapData(p, u, q), b.applyAlpha(a.__owner.__worldAlpha), b.applyColorTransform(a.__owner.__worldColorTransform), b.updateShader()) : (y = b.__initShaderBuffer(g), b.__setShaderBuffer(g), b.applyMatrix(C), b.applyBitmapData(p, !1, q), b.applyAlpha(a.__owner.__worldAlpha), b.applyColorTransform(a.__owner.__worldColorTransform));
                            for (C = r + d; r < C;) {
                                d = Math.min(C - r, h.__quadIndexBufferElements) | 0;
                                if (0 >= d) break;
                                null == g || Yb.maskRender ||
                                    b.__updateShaderBuffer(l);
                                null != y.__position && h.setVertexBufferAt(y.__position.index, a.__quadBuffer.vertexBuffer, 16 * r, 2);
                                null != y.__textureCoord && h.setVertexBufferAt(y.__textureCoord.index, a.__quadBuffer.vertexBuffer, 16 * r + 2, 2);
                                h.drawTriangles(h.__quadIndexBuffer, 0, 2 * d);
                                l += 4 * d;
                                r += d
                            }
                            b.__clearShader()
                        }
                        break;
                    case 9:
                        if (null != m) {
                            switch (c.prev._hx_index) {
                                case 0:
                                    c.oPos += 2;
                                    c.bPos += 2;
                                    break;
                                case 1:
                                    c.iPos += 1;
                                    c.fPos += 1;
                                    break;
                                case 2:
                                    c.oPos += 4;
                                    c.iiPos += 2;
                                    c.ffPos += 1;
                                    c.fPos += 1;
                                    break;
                                case 3:
                                    c.oPos += 1;
                                    break;
                                case 4:
                                    c.fPos +=
                                        6;
                                    break;
                                case 5:
                                    c.fPos += 4;
                                    break;
                                case 6:
                                    c.fPos += 3;
                                    break;
                                case 7:
                                    c.fPos += 4;
                                    break;
                                case 8:
                                    c.oPos += 3;
                                    break;
                                case 9:
                                    c.fPos += 4;
                                    break;
                                case 10:
                                    c.fPos += 5;
                                    c.oPos += 1;
                                    break;
                                case 12:
                                    c.oPos += 4;
                                    break;
                                case 14:
                                    c.oPos += 2;
                                    c.bPos += 2;
                                    break;
                                case 15:
                                    c.oPos += 4;
                                    c.iiPos += 2;
                                    c.ffPos += 1;
                                    c.fPos += 1;
                                    break;
                                case 16:
                                    c.oPos += 4;
                                    c.iPos += 1;
                                    c.fPos += 2;
                                    c.bPos += 1;
                                    break;
                                case 17:
                                    c.fPos += 2;
                                    break;
                                case 18:
                                    c.fPos += 2;
                                    break;
                                case 19:
                                    c.oPos += 1;
                                    break;
                                case 20:
                                    c.oPos += 1
                            }
                            c.prev = da.DRAW_RECT;
                            var t = c;
                            y = t.buffer.f[t.fPos];
                            C = t.buffer.f[t.fPos + 1];
                            d = t.buffer.f[t.fPos +
                                2];
                            f = t.buffer.f[t.fPos + 3];
                            t = m;
                            Yb.tempColorTransform.redOffset = t >>> 16 & 255;
                            Yb.tempColorTransform.greenOffset = t >>> 8 & 255;
                            Yb.tempColorTransform.blueOffset = t & 255;
                            Yb.tempColorTransform.__combine(a.__owner.__worldColorTransform);
                            k.identity();
                            k.scale(d, f);
                            k.tx = y;
                            k.ty = C;
                            k.concat(a.__owner.__renderTransform);
                            d = Yb.maskRender ? b.__maskShader : b.__initGraphicsShader(null);
                            b.setShader(d);
                            b.applyMatrix(b.__getMatrix(k, 1));
                            b.applyBitmapData(Yb.blankBitmapData, !0, q);
                            b.applyAlpha((t >>> 24 & 255) / 255 * a.__owner.__worldAlpha);
                            b.applyColorTransform(Yb.tempColorTransform);
                            b.updateShader();
                            y = Yb.blankBitmapData.getVertexBuffer(h);
                            null != d.__position && h.setVertexBufferAt(d.__position.index, y, 0, 3);
                            null != d.__textureCoord && h.setVertexBufferAt(d.__textureCoord.index, y, 3, 2);
                            d = Yb.blankBitmapData.getIndexBuffer(h);
                            h.drawTriangles(d);
                            l += 4;
                            b.__clearShader()
                        }
                        break;
                    case 12:
                        switch (c.prev._hx_index) {
                            case 0:
                                c.oPos += 2;
                                c.bPos += 2;
                                break;
                            case 1:
                                c.iPos += 1;
                                c.fPos += 1;
                                break;
                            case 2:
                                c.oPos += 4;
                                c.iiPos += 2;
                                c.ffPos += 1;
                                c.fPos += 1;
                                break;
                            case 3:
                                c.oPos += 1;
                                break;
                            case 4:
                                c.fPos += 6;
                                break;
                            case 5:
                                c.fPos += 4;
                                break;
                            case 6:
                                c.fPos += 3;
                                break;
                            case 7:
                                c.fPos += 4;
                                break;
                            case 8:
                                c.oPos += 3;
                                break;
                            case 9:
                                c.fPos += 4;
                                break;
                            case 10:
                                c.fPos += 5;
                                c.oPos += 1;
                                break;
                            case 12:
                                c.oPos += 4;
                                break;
                            case 14:
                                c.oPos += 2;
                                c.bPos += 2;
                                break;
                            case 15:
                                c.oPos += 4;
                                c.iiPos += 2;
                                c.ffPos += 1;
                                c.fPos += 1;
                                break;
                            case 16:
                                c.oPos += 4;
                                c.iPos += 1;
                                c.fPos += 2;
                                c.bPos += 1;
                                break;
                            case 17:
                                c.fPos += 2;
                                break;
                            case 18:
                                c.fPos += 2;
                                break;
                            case 19:
                                c.oPos += 1;
                                break;
                            case 20:
                                c.oPos += 1
                        }
                        c.prev = da.DRAW_TRIANGLES;
                        f = c;
                        y = f.buffer.o[f.oPos + 1];
                        C = f.buffer.o[f.oPos +
                            2];
                        d = f.buffer.o[f.oPos + 3];
                        t = null != y;
                        f = Math.floor(f.buffer.o[f.oPos].get_length() / 2);
                        y = t ? y.get_length() : f;
                        t = (C = null != C && C.get_length() >= 3 * f) ? 4 : 2;
                        f = t + 2;
                        var v = C ? a.__vertexBufferUVT : a.__vertexBuffer,
                            G = C ? x : D,
                            F = b.__getMatrix(a.__owner.__renderTransform, 1);
                        if (null == g || Yb.maskRender) {
                            var I = Yb.maskRender ? b.__maskShader : b.__initGraphicsShader(null);
                            b.setShader(I);
                            b.applyMatrix(F);
                            b.applyBitmapData(p, u, q);
                            b.applyAlpha(a.__owner.__worldAlpha);
                            b.applyColorTransform(a.__owner.__worldColorTransform);
                            b.updateShader()
                        } else I =
                            b.__initShaderBuffer(g), b.__setShaderBuffer(g), b.applyMatrix(F), b.applyBitmapData(p, !1, q), b.applyAlpha(1), b.applyColorTransform(null), b.__updateShaderBuffer(l);
                        null != I.__position && h.setVertexBufferAt(I.__position.index, v, G, C ? 4 : 2);
                        null != I.__textureCoord && h.setVertexBufferAt(I.__textureCoord.index, v, G + t, 2);
                        switch (d) {
                            case 0:
                                h.setCulling(0);
                                break;
                            case 1:
                                h.setCulling(3);
                                break;
                            case 2:
                                h.setCulling(1)
                        }
                        h.__drawTriangles(0, y);
                        l += y;
                        C ? x += f * y : D += f * y;
                        switch (d) {
                            case 1:
                            case 2:
                                h.setCulling(0)
                        }
                        b.__clearShader();
                        break;
                    case 13:
                        g = m = p = null;
                        switch (c.prev._hx_index) {
                            case 0:
                                c.oPos += 2;
                                c.bPos += 2;
                                break;
                            case 1:
                                c.iPos += 1;
                                c.fPos += 1;
                                break;
                            case 2:
                                c.oPos += 4;
                                c.iiPos += 2;
                                c.ffPos += 1;
                                c.fPos += 1;
                                break;
                            case 3:
                                c.oPos += 1;
                                break;
                            case 4:
                                c.fPos += 6;
                                break;
                            case 5:
                                c.fPos += 4;
                                break;
                            case 6:
                                c.fPos += 3;
                                break;
                            case 7:
                                c.fPos += 4;
                                break;
                            case 8:
                                c.oPos += 3;
                                break;
                            case 9:
                                c.fPos += 4;
                                break;
                            case 10:
                                c.fPos += 5;
                                c.oPos += 1;
                                break;
                            case 12:
                                c.oPos += 4;
                                break;
                            case 14:
                                c.oPos += 2;
                                c.bPos += 2;
                                break;
                            case 15:
                                c.oPos += 4;
                                c.iiPos += 2;
                                c.ffPos += 1;
                                c.fPos += 1;
                                break;
                            case 16:
                                c.oPos += 4;
                                c.iPos += 1;
                                c.fPos += 2;
                                c.bPos += 1;
                                break;
                            case 17:
                                c.fPos += 2;
                                break;
                            case 18:
                                c.fPos += 2;
                                break;
                            case 19:
                                c.oPos += 1;
                                break;
                            case 20:
                                c.oPos += 1
                        }
                        c.prev = d;
                        h.setCulling(3);
                        break;
                    case 18:
                        switch (c.prev._hx_index) {
                            case 0:
                                c.oPos += 2;
                                c.bPos += 2;
                                break;
                            case 1:
                                c.iPos += 1;
                                c.fPos += 1;
                                break;
                            case 2:
                                c.oPos += 4;
                                c.iiPos += 2;
                                c.ffPos += 1;
                                c.fPos += 1;
                                break;
                            case 3:
                                c.oPos += 1;
                                break;
                            case 4:
                                c.fPos += 6;
                                break;
                            case 5:
                                c.fPos += 4;
                                break;
                            case 6:
                                c.fPos += 3;
                                break;
                            case 7:
                                c.fPos += 4;
                                break;
                            case 8:
                                c.oPos += 3;
                                break;
                            case 9:
                                c.fPos += 4;
                                break;
                            case 10:
                                c.fPos += 5;
                                c.oPos += 1;
                                break;
                            case 12:
                                c.oPos +=
                                    4;
                                break;
                            case 14:
                                c.oPos += 2;
                                c.bPos += 2;
                                break;
                            case 15:
                                c.oPos += 4;
                                c.iiPos += 2;
                                c.ffPos += 1;
                                c.fPos += 1;
                                break;
                            case 16:
                                c.oPos += 4;
                                c.iPos += 1;
                                c.fPos += 2;
                                c.bPos += 1;
                                break;
                            case 17:
                                c.fPos += 2;
                                break;
                            case 18:
                                c.fPos += 2;
                                break;
                            case 19:
                                c.oPos += 1;
                                break;
                            case 20:
                                c.oPos += 1
                        }
                        c.prev = da.MOVE_TO;
                        break;
                    case 19:
                        switch (c.prev._hx_index) {
                            case 0:
                                c.oPos += 2;
                                c.bPos += 2;
                                break;
                            case 1:
                                c.iPos += 1;
                                c.fPos += 1;
                                break;
                            case 2:
                                c.oPos += 4;
                                c.iiPos += 2;
                                c.ffPos += 1;
                                c.fPos += 1;
                                break;
                            case 3:
                                c.oPos += 1;
                                break;
                            case 4:
                                c.fPos += 6;
                                break;
                            case 5:
                                c.fPos += 4;
                                break;
                            case 6:
                                c.fPos +=
                                    3;
                                break;
                            case 7:
                                c.fPos += 4;
                                break;
                            case 8:
                                c.oPos += 3;
                                break;
                            case 9:
                                c.fPos += 4;
                                break;
                            case 10:
                                c.fPos += 5;
                                c.oPos += 1;
                                break;
                            case 12:
                                c.oPos += 4;
                                break;
                            case 14:
                                c.oPos += 2;
                                c.bPos += 2;
                                break;
                            case 15:
                                c.oPos += 4;
                                c.iiPos += 2;
                                c.ffPos += 1;
                                c.fPos += 1;
                                break;
                            case 16:
                                c.oPos += 4;
                                c.iPos += 1;
                                c.fPos += 2;
                                c.bPos += 1;
                                break;
                            case 17:
                                c.fPos += 2;
                                break;
                            case 18:
                                c.fPos += 2;
                                break;
                            case 19:
                                c.oPos += 1;
                                break;
                            case 20:
                                c.oPos += 1
                        }
                        c.prev = da.OVERRIDE_BLEND_MODE;
                        d = c;
                        b.__setBlendMode(d.buffer.o[d.oPos]);
                        break;
                    default:
                        switch (c.prev._hx_index) {
                            case 0:
                                c.oPos += 2;
                                c.bPos +=
                                    2;
                                break;
                            case 1:
                                c.iPos += 1;
                                c.fPos += 1;
                                break;
                            case 2:
                                c.oPos += 4;
                                c.iiPos += 2;
                                c.ffPos += 1;
                                c.fPos += 1;
                                break;
                            case 3:
                                c.oPos += 1;
                                break;
                            case 4:
                                c.fPos += 6;
                                break;
                            case 5:
                                c.fPos += 4;
                                break;
                            case 6:
                                c.fPos += 3;
                                break;
                            case 7:
                                c.fPos += 4;
                                break;
                            case 8:
                                c.oPos += 3;
                                break;
                            case 9:
                                c.fPos += 4;
                                break;
                            case 10:
                                c.fPos += 5;
                                c.oPos += 1;
                                break;
                            case 12:
                                c.oPos += 4;
                                break;
                            case 14:
                                c.oPos += 2;
                                c.bPos += 2;
                                break;
                            case 15:
                                c.oPos += 4;
                                c.iiPos += 2;
                                c.ffPos += 1;
                                c.fPos += 1;
                                break;
                            case 16:
                                c.oPos += 4;
                                c.iPos += 1;
                                c.fPos += 2;
                                c.bPos += 1;
                                break;
                            case 17:
                                c.fPos += 2;
                                break;
                            case 18:
                                c.fPos += 2;
                                break;
                            case 19:
                                c.oPos += 1;
                                break;
                            case 20:
                                c.oPos += 1
                        }
                        c.prev = d
                }
                ua.__pool.release(k)
            }
            a.__hardwareDirty = !1;
            a.set___dirty(!1)
        }
};
Yb.renderMask = function(a, b) {
    Yb.maskRender = !0;
    Yb.render(a, b);
    Yb.maskRender = !1
};
Yb.resizeVertexBuffer = function(a, b, c) {
    var d = b ? a.__vertexBufferDataUVT : a.__vertexBufferData,
        f = null;
    if (null == d) {
        var h = f = null,
            k = null,
            g = null,
            p = null;
        f = c = null != c ? new Float32Array(c) : null != f ? new Float32Array(f) : null != h ? new Float32Array(h.__array) : null != k ? new Float32Array(k) : null != g ? null == p ? new Float32Array(g, 0) :
            new Float32Array(g, 0, p) : null
    } else c > d.length && (p = g = k = h = f = null, f = c = null != c ? new Float32Array(c) : null != f ? new Float32Array(f) : null != h ? new Float32Array(h.__array) : null != k ? new Float32Array(k) : null != g ? null == p ? new Float32Array(g, 0) : new Float32Array(g, 0, p) : null, f.set(d));
    null != f && (b ? a.__vertexBufferDataUVT = f : a.__vertexBufferData = f)
};
var Xf = function() {
    null == this.__glFragmentSource && (this.__glFragmentSource = "varying vec2 openfl_TextureCoordv;\n\n\t\tuniform sampler2D openfl_Texture;\n\n\t\tvoid main(void) {\n\n\t\t\tvec4 color = texture2D (openfl_Texture, openfl_TextureCoordv);\n\n\t\t\tif (color.a == 0.0) {\n\n\t\t\t\tdiscard;\n\n\t\t\t} else {\n\n\t\t\t\tgl_FragColor = color;\n\n\t\t\t}\n\n\t\t}");
    null == this.__glVertexSource && (this.__glVertexSource = "attribute vec4 openfl_Position;\n\t\tattribute vec2 openfl_TextureCoord;\n\t\tvarying vec2 openfl_TextureCoordv;\n\n\t\tuniform mat4 openfl_Matrix;\n\n\t\tvoid main(void) {\n\n\t\t\topenfl_TextureCoordv = openfl_TextureCoord;\n\n\t\t\tgl_Position = openfl_Matrix * openfl_Position;\n\n\t\t}");
    Dd.call(this);
    this.__isGenerated = !0;
    this.__initGL()
};
g["openfl.display._internal.Context3DMaskShader"] = Xf;
Xf.__name__ = "openfl.display._internal.Context3DMaskShader";
Xf.__super__ = Dd;
Xf.prototype = v(Dd.prototype, {
    __class__: Xf
});
var Ee = function() {};
g["openfl.display._internal.Context3DShape"] = Ee;
Ee.__name__ = "openfl.display._internal.Context3DShape";
Ee.render = function(a, b) {
    if (a.__renderable && !(0 >= a.__worldAlpha)) {
        var c = a.__graphics;
        if (null != c) {
            b.__setBlendMode(a.__worldBlendMode);
            b.__pushMaskObject(a);
            Yb.render(c, b);
            if (null != c.__bitmap && c.__visible) {
                var d = b.__context3D,
                    f = b.__initDisplayShader(a.__worldShader);
                b.setShader(f);
                b.applyBitmapData(c.__bitmap, !0);
                var h =
                    ua.__pool.get();
                h.scale(1 / c.__bitmapScale, 1 / c.__bitmapScale);
                h.concat(c.__worldTransform);
                b.applyMatrix(b.__getMatrix(h, 1));
                ua.__pool.release(h);
                b.applyAlpha(a.__worldAlpha);
                b.applyColorTransform(a.__worldColorTransform);
                b.updateShader();
                h = c.__bitmap.getVertexBuffer(d);
                null != f.__position && d.setVertexBufferAt(f.__position.index, h, 0, 3);
                null != f.__textureCoord && d.setVertexBufferAt(f.__textureCoord.index, h, 3, 2);
                c = c.__bitmap.getIndexBuffer(d);
                d.drawTriangles(c);
                b.__clearShader()
            }
            b.__popMaskObject(a)
        }
    }
};
Ee.renderMask = function(a, b) {
    var c = a.__graphics;
    if (null != c && (Yb.renderMask(c, b), null != c.__bitmap)) {
        a = b.__context3D;
        var d = b.__maskShader;
        b.setShader(d);
        b.applyBitmapData(c.__bitmap, !0);
        b.applyMatrix(b.__getMatrix(c.__worldTransform, 1));
        b.updateShader();
        var f = c.__bitmap.getVertexBuffer(a);
        null != d.__position && a.setVertexBufferAt(d.__position.index, f, 0, 3);
        null != d.__textureCoord && a.setVertexBufferAt(d.__textureCoord.index, f, 3, 2);
        c = c.__bitmap.getIndexBuffer(a);
        a.drawTriangles(c);
        b.__clearShader()
    }
};
var uj =
    function() {};
g["openfl.display._internal.Context3DSimpleButton"] = uj;
uj.__name__ = "openfl.display._internal.Context3DSimpleButton";
uj.renderDrawable = function(a, b) {
    !a.__renderable || 0 >= a.__worldAlpha || null == a.__currentState || (b.__pushMaskObject(a), b.__renderDrawable(a.__currentState), b.__popMaskObject(a), b.__renderEvent(a))
};
uj.renderDrawableMask = function(a, b) {
    null != a.__currentState && b.__renderDrawableMask(a.__currentState)
};
var bf = function() {};
g["openfl.display._internal.Context3DTextField"] = bf;
bf.__name__ =
    "openfl.display._internal.Context3DTextField";
bf.render = function(a, b) {
    b.__softwareRenderer.__pixelRatio = b.__pixelRatio;
    var c = b.__softwareRenderer;
    b = a.__textEngine;
    var d = !(b.background || b.border),
        f = d ? b.textBounds : b.bounds,
        h = a.__graphics,
        k = 0;
    if (a.__dirty) {
        a.__updateLayout();
        null == h.__bounds && (h.__bounds = new na);
        if (0 == a.get_text().length) {
            k = b.bounds.width - 4;
            var g = a.get_defaultTextFormat().align;
            k = 3 == g ? 0 : 4 == g ? k : k / 2;
            switch (g) {
                case 0:
                    k += a.get_defaultTextFormat().leftMargin / 2;
                    k -= a.get_defaultTextFormat().rightMargin /
                        2;
                    k += a.get_defaultTextFormat().indent / 2;
                    k += a.get_defaultTextFormat().blockIndent / 2;
                    break;
                case 2:
                    k += a.get_defaultTextFormat().leftMargin;
                    k += a.get_defaultTextFormat().indent;
                    k += a.get_defaultTextFormat().blockIndent;
                    break;
                case 3:
                    k += a.get_defaultTextFormat().leftMargin;
                    k += a.get_defaultTextFormat().indent;
                    k += a.get_defaultTextFormat().blockIndent;
                    break;
                case 4:
                    k -= a.get_defaultTextFormat().rightMargin
            }
            d && (f.y = b.bounds.y, f.x = k)
        }
        h.__bounds.copyFrom(f)
    }
    g = c.__pixelRatio;
    h.__update(c.__worldTransform, g);
    if (a.__dirty ||
        h.__softwareDirty) {
        var p = Math.round(h.__width * g),
            q = Math.round(h.__height * g);
        if (!(null != b.text && "" != b.text || b.background || b.border || b.__hasFocus || 1 == b.type && b.selectable) || (0 >= b.width || 0 >= b.height) && 2 != b.autoSize) a.__graphics.__canvas = null, a.__graphics.__context = null, a.__graphics.__bitmap = null, a.__graphics.__softwareDirty = !1, a.__graphics.set___dirty(!1), a.__dirty = !1;
        else {
            null == a.__graphics.__canvas && (a.__graphics.__canvas = window.document.createElement("canvas"), a.__graphics.__context = a.__graphics.__canvas.getContext("2d"));
            Q.context = h.__context;
            h.__canvas.width = p;
            h.__canvas.height = q;
            c.__isDOM && (h.__canvas.style.width = Math.round(p / g) + "px", h.__canvas.style.height = Math.round(q / g) + "px");
            p = ua.__pool.get();
            p.scale(g, g);
            p.concat(h.__renderTransform);
            Q.context.setTransform(p.a, p.b, p.c, p.d, p.tx, p.ty);
            ua.__pool.release(p);
            null == Q.clearRect && (Q.clearRect = "undefined" !== typeof navigator && "undefined" !== typeof navigator.isCocoonJS);
            Q.clearRect && Q.context.clearRect(0, 0, h.__canvas.width, h.__canvas.height);
            if (null != b.text && "" != b.text ||
                b.__hasFocus) {
                d = b.text;
                h.__context.imageSmoothingEnabled = !c.__allowSmoothing || 0 == b.antiAliasType && 400 == b.sharpness ? !1 : !0;
                if (b.border || b.background) {
                    Q.context.rect(.5, .5, f.width - 1, f.height - 1);
                    if (b.background) {
                        var u = O.hex(b.backgroundColor & 16777215, 6);
                        Q.context.fillStyle = "#" + u;
                        Q.context.fill()
                    }
                    b.border && (Q.context.lineWidth = 1, u = O.hex(b.borderColor & 16777215, 6), Q.context.strokeStyle = "#" + u, Q.context.stroke())
                }
                Q.context.textBaseline = "alphabetic";
                Q.context.textAlign = "start";
                c = -a.get_scrollH();
                var m = k = 0;
                for (u = a.get_scrollV() - 1; m < u;) {
                    var r = m++;
                    k -= b.lineHeights.get(r)
                }
                var l;
                for (p = b.layoutGroups.iterator(); p.hasNext();)
                    if (q = p.next(), !(q.lineIndex < a.get_scrollV() - 1)) {
                        if (q.lineIndex > b.get_bottomScrollV() - 1) break;
                        var D = "#" + O.hex(q.format.color & 16777215, 6);
                        Q.context.font = Nb.getFont(q.format);
                        Q.context.fillStyle = D;
                        Q.context.fillText(d.substring(q.startIndex, q.endIndex), q.offsetX + c - f.x, q.offsetY + q.ascent + k - f.y);
                        if (-1 < a.__caretIndex && b.selectable)
                            if (a.__selectionIndex == a.__caretIndex) {
                                if (a.__showCursor && q.startIndex <=
                                    a.__caretIndex && q.endIndex >= a.__caretIndex) {
                                    m = l = 0;
                                    for (u = a.__caretIndex - q.startIndex; m < u;) {
                                        r = m++;
                                        if (q.positions.length <= r) break;
                                        l += q.positions[r]
                                    }
                                    m = 0;
                                    u = a.get_scrollV();
                                    for (r = q.lineIndex + 1; u < r;) {
                                        var x = u++;
                                        m += b.lineHeights.get(x - 1)
                                    }
                                    Q.context.beginPath();
                                    u = O.hex(q.format.color & 16777215, 6);
                                    Q.context.strokeStyle = "#" + u;
                                    Q.context.moveTo(q.offsetX + l - a.get_scrollH() - f.x, m + 2 - f.y);
                                    Q.context.lineWidth = 1;
                                    Q.context.lineTo(q.offsetX + l - a.get_scrollH() - f.x, m + Nb.getFormatHeight(a.get_defaultTextFormat()) - 1 - f.y);
                                    Q.context.stroke();
                                    Q.context.closePath()
                                }
                            } else if (q.startIndex <= a.__caretIndex && q.endIndex >= a.__caretIndex || q.startIndex <= a.__selectionIndex && q.endIndex >= a.__selectionIndex || q.startIndex > a.__caretIndex && q.endIndex < a.__selectionIndex || q.startIndex > a.__selectionIndex && q.endIndex < a.__caretIndex) l = Math.min(a.__selectionIndex, a.__caretIndex) | 0, m = Math.max(a.__selectionIndex, a.__caretIndex) | 0, q.startIndex > l && (l = q.startIndex), q.endIndex < m && (m = q.endIndex), r = a.getCharBoundaries(l), m >= q.endIndex ? (u = a.getCharBoundaries(q.endIndex -
                            1), null != u && (u.x += u.width + 2)) : u = a.getCharBoundaries(m), null != r && null != u && (Q.context.fillStyle = "#000000", Q.context.fillRect(r.x + c - f.x, r.y + k, u.x - r.x, q.height), Q.context.fillStyle = "#FFFFFF", Q.context.fillText(d.substring(l, m), c + r.x - f.x, q.offsetY + q.ascent + k));
                        q.format.underline && (Q.context.beginPath(), Q.context.strokeStyle = D, Q.context.lineWidth = 1, D = q.offsetX + c - f.x, l = Math.ceil(q.offsetY + k + q.ascent - f.y) + Math.floor(.185 * q.ascent) + .5, Q.context.moveTo(D, l), Q.context.lineTo(D + q.width, l), Q.context.stroke(),
                            Q.context.closePath())
                    }
            } else {
                if (b.border || b.background) b.border ? Q.context.rect(.5, .5, f.width - 1, f.height - 1) : Q.context.rect(0, 0, f.width, f.height), b.background && (u = O.hex(b.backgroundColor & 16777215, 6), Q.context.fillStyle = "#" + u, Q.context.fill()), b.border && (Q.context.lineWidth = 1, Q.context.lineCap = "square", u = O.hex(b.borderColor & 16777215, 6), Q.context.strokeStyle = "#" + u, Q.context.stroke());
                if (-1 < a.__caretIndex && b.selectable && a.__showCursor) {
                    c = -a.get_scrollH() + (d ? 0 : k);
                    m = k = 0;
                    for (u = a.get_scrollV() - 1; m < u;) r = m++,
                        k += b.lineHeights.get(r);
                    Q.context.beginPath();
                    u = O.hex(a.get_defaultTextFormat().color & 16777215, 6);
                    Q.context.strokeStyle = "#" + u;
                    Q.context.moveTo(c + 2.5, k + 2.5);
                    Q.context.lineWidth = 1;
                    Q.context.lineTo(c + 2.5, k + Nb.getFormatHeight(a.get_defaultTextFormat()) - 1);
                    Q.context.stroke();
                    Q.context.closePath()
                }
            }
            h.__bitmap = Fb.fromCanvas(a.__graphics.__canvas);
            h.__bitmapScale = g;
            h.__visible = !0;
            a.__dirty = !1;
            h.__softwareDirty = !1;
            h.set___dirty(!1)
        }
    }
    a.__graphics.__hardwareDirty = !1
};
bf.renderDrawable = function(a, b) {
    b.__updateCacheBitmap(a,
        !1);
    if (null == a.__cacheBitmap || a.__isCacheBitmapRender) {
        if (bf.render(a, b), !(null == a.opaqueBackground && null == a.__graphics || !a.__renderable || 0 >= a.__worldAlpha)) {
            if (null != a.opaqueBackground && !a.__isCacheBitmapRender && 0 < a.get_width() && 0 < a.get_height()) {
                b.__setBlendMode(a.__worldBlendMode);
                b.__pushMaskObject(a);
                var c = b.__context3D,
                    d = na.__pool.get();
                d.setTo(0, 0, a.get_width(), a.get_height());
                b.__pushMaskRect(d, a.__renderTransform);
                var f = a.opaqueBackground;
                c.clear((f >>> 16 & 255) / 255, (f >>> 8 & 255) / 255, (f & 255) /
                    255, 1, 0, 0, 1);
                b.__popMaskRect();
                b.__popMaskObject(a);
                na.__pool.release(d)
            }
            null != a.__graphics && Ee.render(a, b)
        }
    } else Vd.render(a.__cacheBitmap, b);
    b.__renderEvent(a)
};
bf.renderDrawableMask = function(a, b) {
    bf.renderMask(a, b);
    Yf.renderDrawableMask(a, b)
};
bf.renderMask = function(a, b) {
    var c = b.__softwareRenderer;
    b = a.__textEngine;
    var d = !(b.background || b.border),
        f = d ? b.textBounds : b.bounds,
        h = a.__graphics,
        k = 0;
    if (a.__dirty) {
        a.__updateLayout();
        null == h.__bounds && (h.__bounds = new na);
        if (0 == a.get_text().length) {
            k = b.bounds.width -
                4;
            var g = a.get_defaultTextFormat().align;
            k = 3 == g ? 0 : 4 == g ? k : k / 2;
            switch (g) {
                case 0:
                    k += a.get_defaultTextFormat().leftMargin / 2;
                    k -= a.get_defaultTextFormat().rightMargin / 2;
                    k += a.get_defaultTextFormat().indent / 2;
                    k += a.get_defaultTextFormat().blockIndent / 2;
                    break;
                case 2:
                    k += a.get_defaultTextFormat().leftMargin;
                    k += a.get_defaultTextFormat().indent;
                    k += a.get_defaultTextFormat().blockIndent;
                    break;
                case 3:
                    k += a.get_defaultTextFormat().leftMargin;
                    k += a.get_defaultTextFormat().indent;
                    k += a.get_defaultTextFormat().blockIndent;
                    break;
                case 4:
                    k -= a.get_defaultTextFormat().rightMargin
            }
            d && (f.y = b.bounds.y, f.x = k)
        }
        h.__bounds.copyFrom(f)
    }
    g = c.__pixelRatio;
    h.__update(c.__worldTransform, g);
    if (a.__dirty || h.__softwareDirty) {
        var p = Math.round(h.__width * g),
            q = Math.round(h.__height * g);
        if (!(null != b.text && "" != b.text || b.background || b.border || b.__hasFocus || 1 == b.type && b.selectable) || (0 >= b.width || 0 >= b.height) && 2 != b.autoSize) a.__graphics.__canvas = null, a.__graphics.__context = null, a.__graphics.__bitmap = null, a.__graphics.__softwareDirty = !1, a.__graphics.set___dirty(!1),
            a.__dirty = !1;
        else {
            null == a.__graphics.__canvas && (a.__graphics.__canvas = window.document.createElement("canvas"), a.__graphics.__context = a.__graphics.__canvas.getContext("2d"));
            Q.context = h.__context;
            h.__canvas.width = p;
            h.__canvas.height = q;
            c.__isDOM && (h.__canvas.style.width = Math.round(p / g) + "px", h.__canvas.style.height = Math.round(q / g) + "px");
            p = ua.__pool.get();
            p.scale(g, g);
            p.concat(h.__renderTransform);
            Q.context.setTransform(p.a, p.b, p.c, p.d, p.tx, p.ty);
            ua.__pool.release(p);
            null == Q.clearRect && (Q.clearRect = "undefined" !==
                typeof navigator && "undefined" !== typeof navigator.isCocoonJS);
            Q.clearRect && Q.context.clearRect(0, 0, h.__canvas.width, h.__canvas.height);
            if (null != b.text && "" != b.text || b.__hasFocus) {
                d = b.text;
                h.__context.imageSmoothingEnabled = !c.__allowSmoothing || 0 == b.antiAliasType && 400 == b.sharpness ? !1 : !0;
                if (b.border || b.background) {
                    Q.context.rect(.5, .5, f.width - 1, f.height - 1);
                    if (b.background) {
                        var u = O.hex(b.backgroundColor & 16777215, 6);
                        Q.context.fillStyle = "#" + u;
                        Q.context.fill()
                    }
                    b.border && (Q.context.lineWidth = 1, u = O.hex(b.borderColor &
                        16777215, 6), Q.context.strokeStyle = "#" + u, Q.context.stroke())
                }
                Q.context.textBaseline = "alphabetic";
                Q.context.textAlign = "start";
                c = -a.get_scrollH();
                var m = k = 0;
                for (u = a.get_scrollV() - 1; m < u;) {
                    var r = m++;
                    k -= b.lineHeights.get(r)
                }
                var l;
                for (p = b.layoutGroups.iterator(); p.hasNext();)
                    if (q = p.next(), !(q.lineIndex < a.get_scrollV() - 1)) {
                        if (q.lineIndex > b.get_bottomScrollV() - 1) break;
                        var D = "#" + O.hex(q.format.color & 16777215, 6);
                        Q.context.font = Nb.getFont(q.format);
                        Q.context.fillStyle = D;
                        Q.context.fillText(d.substring(q.startIndex,
                            q.endIndex), q.offsetX + c - f.x, q.offsetY + q.ascent + k - f.y);
                        if (-1 < a.__caretIndex && b.selectable)
                            if (a.__selectionIndex == a.__caretIndex) {
                                if (a.__showCursor && q.startIndex <= a.__caretIndex && q.endIndex >= a.__caretIndex) {
                                    m = l = 0;
                                    for (u = a.__caretIndex - q.startIndex; m < u;) {
                                        r = m++;
                                        if (q.positions.length <= r) break;
                                        l += q.positions[r]
                                    }
                                    m = 0;
                                    u = a.get_scrollV();
                                    for (r = q.lineIndex + 1; u < r;) {
                                        var x = u++;
                                        m += b.lineHeights.get(x - 1)
                                    }
                                    Q.context.beginPath();
                                    u = O.hex(q.format.color & 16777215, 6);
                                    Q.context.strokeStyle = "#" + u;
                                    Q.context.moveTo(q.offsetX +
                                        l - a.get_scrollH() - f.x, m + 2 - f.y);
                                    Q.context.lineWidth = 1;
                                    Q.context.lineTo(q.offsetX + l - a.get_scrollH() - f.x, m + Nb.getFormatHeight(a.get_defaultTextFormat()) - 1 - f.y);
                                    Q.context.stroke();
                                    Q.context.closePath()
                                }
                            } else if (q.startIndex <= a.__caretIndex && q.endIndex >= a.__caretIndex || q.startIndex <= a.__selectionIndex && q.endIndex >= a.__selectionIndex || q.startIndex > a.__caretIndex && q.endIndex < a.__selectionIndex || q.startIndex > a.__selectionIndex && q.endIndex < a.__caretIndex) l = Math.min(a.__selectionIndex, a.__caretIndex) | 0, m = Math.max(a.__selectionIndex,
                            a.__caretIndex) | 0, q.startIndex > l && (l = q.startIndex), q.endIndex < m && (m = q.endIndex), r = a.getCharBoundaries(l), m >= q.endIndex ? (u = a.getCharBoundaries(q.endIndex - 1), null != u && (u.x += u.width + 2)) : u = a.getCharBoundaries(m), null != r && null != u && (Q.context.fillStyle = "#000000", Q.context.fillRect(r.x + c - f.x, r.y + k, u.x - r.x, q.height), Q.context.fillStyle = "#FFFFFF", Q.context.fillText(d.substring(l, m), c + r.x - f.x, q.offsetY + q.ascent + k));
                        q.format.underline && (Q.context.beginPath(), Q.context.strokeStyle = D, Q.context.lineWidth = 1, D = q.offsetX +
                            c - f.x, l = Math.ceil(q.offsetY + k + q.ascent - f.y) + Math.floor(.185 * q.ascent) + .5, Q.context.moveTo(D, l), Q.context.lineTo(D + q.width, l), Q.context.stroke(), Q.context.closePath())
                    }
            } else {
                if (b.border || b.background) b.border ? Q.context.rect(.5, .5, f.width - 1, f.height - 1) : Q.context.rect(0, 0, f.width, f.height), b.background && (u = O.hex(b.backgroundColor & 16777215, 6), Q.context.fillStyle = "#" + u, Q.context.fill()), b.border && (Q.context.lineWidth = 1, Q.context.lineCap = "square", u = O.hex(b.borderColor & 16777215, 6), Q.context.strokeStyle =
                    "#" + u, Q.context.stroke());
                if (-1 < a.__caretIndex && b.selectable && a.__showCursor) {
                    c = -a.get_scrollH() + (d ? 0 : k);
                    m = k = 0;
                    for (u = a.get_scrollV() - 1; m < u;) r = m++, k += b.lineHeights.get(r);
                    Q.context.beginPath();
                    u = O.hex(a.get_defaultTextFormat().color & 16777215, 6);
                    Q.context.strokeStyle = "#" + u;
                    Q.context.moveTo(c + 2.5, k + 2.5);
                    Q.context.lineWidth = 1;
                    Q.context.lineTo(c + 2.5, k + Nb.getFormatHeight(a.get_defaultTextFormat()) - 1);
                    Q.context.stroke();
                    Q.context.closePath()
                }
            }
            h.__bitmap = Fb.fromCanvas(a.__graphics.__canvas);
            h.__bitmapScale =
                g;
            h.__visible = !0;
            a.__dirty = !1;
            h.__softwareDirty = !1;
            h.set___dirty(!1)
        }
    }
    a.__graphics.__hardwareDirty = !1
};
var V = function() {};
g["openfl.display._internal.Context3DTilemap"] = V;
V.__name__ = "openfl.display._internal.Context3DTilemap";
V.buildBuffer = function(a, b) {
    if (!a.__renderable || 0 == a.__group.__tiles.length || 0 >= a.__worldAlpha) a.__group.__dirty = !1;
    else {
        V.numTiles = 0;
        V.vertexBufferData = null != a.__buffer ? a.__buffer.vertexBufferData : null;
        V.vertexDataPosition = 0;
        var c = na.__pool.get(),
            d = ua.__pool.get(),
            f = ua.__pool.get();
        V.dataPerVertex = 4;
        a.tileAlphaEnabled && V.dataPerVertex++;
        a.tileColorTransformEnabled && (V.dataPerVertex += 8);
        V.buildBufferTileContainer(a, a.__group, b, f, a.__tileset, a.tileAlphaEnabled, a.__worldAlpha, a.tileColorTransformEnabled, a.__worldColorTransform, null, c, d);
        a.__buffer.flushVertexBufferData();
        na.__pool.release(c);
        ua.__pool.release(d);
        ua.__pool.release(f)
    }
};
V.buildBufferTileContainer = function(a, b, c, d, f, h, k, g, p, q, u, m, r) {
    null == r && (r = !0);
    var n = ua.__pool.get(),
        l = c.__roundPixels,
        D = b.__tiles;
    r && V.resizeBuffer(a,
        V.numTiles + V.getRecursiveLength(b));
    r = null;
    for (var x, w, J, y, z, C, t, v, P, G, F, I = h ? 5 : 4, eb = 0; eb < D.length;) {
        y = D[eb];
        ++eb;
        n.setTo(1, 0, 0, 1, -y.get_originX(), -y.get_originY());
        n.concat(y.get_matrix());
        n.concat(d);
        l && (n.tx = Math.round(n.tx), n.ty = Math.round(n.ty));
        var K = null != y.get_tileset() ? y.get_tileset() : f;
        var H = y.get_alpha() * k;
        var O = y.get_visible();
        y.__dirty = !1;
        if (O && !(0 >= H))
            if (g && (null != y.get_colorTransform() ? null == p ? r = y.get_colorTransform() : (null == V.cacheColorTransform && (V.cacheColorTransform = new Tb), r = V.cacheColorTransform,
                        r.redMultiplier = p.redMultiplier * y.get_colorTransform().redMultiplier, r.greenMultiplier = p.greenMultiplier * y.get_colorTransform().greenMultiplier, r.blueMultiplier = p.blueMultiplier * y.get_colorTransform().blueMultiplier, r.alphaMultiplier = p.alphaMultiplier * y.get_colorTransform().alphaMultiplier, r.redOffset = p.redOffset + y.get_colorTransform().redOffset, r.greenOffset = p.greenOffset + y.get_colorTransform().greenOffset, r.blueOffset = p.blueOffset + y.get_colorTransform().blueOffset, r.alphaOffset = p.alphaOffset + y.get_colorTransform().alphaOffset) :
                    r = p), h || (H = 1), 0 < y.__length) V.buildBufferTileContainer(a, y, c, n, K, h, H, g, r, q, u, m, !1);
            else if (null != K && (O = y.get_id(), x = K.__bitmapData, null != x)) {
            if (-1 == O) {
                w = y.__rect;
                if (null == w || 0 >= w.width || 0 >= w.height) continue;
                K = w.x / x.width;
                y = w.y / x.height;
                O = w.get_right() / x.width;
                z = w.get_bottom() / x.height
            } else {
                x = K.__data[O];
                if (null == x) continue;
                u.setTo(x.x, x.y, x.width, x.height);
                w = u;
                K = x.__uvX;
                y = x.__uvY;
                O = x.__uvWidth;
                z = x.__uvHeight
            }
            x = w.width;
            J = w.height;
            w = 0 * n.a + 0 * n.c + n.tx;
            C = 0 * n.b + 0 * n.d + n.ty;
            t = x * n.a + 0 * n.c + n.tx;
            v = x * n.b + 0 * n.d +
                n.ty;
            P = 0 * n.a + J * n.c + n.tx;
            G = 0 * n.b + J * n.d + n.ty;
            F = x * n.a + J * n.c + n.tx;
            J = x * n.b + J * n.d + n.ty;
            x = V.vertexDataPosition;
            V.vertexBufferData[x] = w;
            V.vertexBufferData[x + 1] = C;
            V.vertexBufferData[x + 2] = K;
            V.vertexBufferData[x + 3] = y;
            V.vertexBufferData[x + V.dataPerVertex] = t;
            V.vertexBufferData[x + V.dataPerVertex + 1] = v;
            V.vertexBufferData[x + V.dataPerVertex + 2] = O;
            V.vertexBufferData[x + V.dataPerVertex + 3] = y;
            V.vertexBufferData[x + 2 * V.dataPerVertex] = P;
            V.vertexBufferData[x + 2 * V.dataPerVertex + 1] = G;
            V.vertexBufferData[x + 2 * V.dataPerVertex + 2] = K;
            V.vertexBufferData[x + 2 * V.dataPerVertex + 3] = z;
            V.vertexBufferData[x + 3 * V.dataPerVertex] = F;
            V.vertexBufferData[x + 3 * V.dataPerVertex + 1] = J;
            V.vertexBufferData[x + 3 * V.dataPerVertex + 2] = O;
            V.vertexBufferData[x + 3 * V.dataPerVertex + 3] = z;
            h && (V.vertexBufferData[x + 0 * V.dataPerVertex + 4] = H, V.vertexBufferData[x + V.dataPerVertex + 4] = H, V.vertexBufferData[x + 2 * V.dataPerVertex + 4] = H, V.vertexBufferData[x + 3 * V.dataPerVertex + 4] = H);
            if (g)
                if (null != r)
                    for (H = 0; 4 > H;) K = H++, V.vertexBufferData[x + V.dataPerVertex * K + I] = r.redMultiplier, V.vertexBufferData[x +
                        V.dataPerVertex * K + I + 1] = r.greenMultiplier, V.vertexBufferData[x + V.dataPerVertex * K + I + 2] = r.blueMultiplier, V.vertexBufferData[x + V.dataPerVertex * K + I + 3] = r.alphaMultiplier, V.vertexBufferData[x + V.dataPerVertex * K + I + 4] = r.redOffset, V.vertexBufferData[x + V.dataPerVertex * K + I + 5] = r.greenOffset, V.vertexBufferData[x + V.dataPerVertex * K + I + 6] = r.blueOffset, V.vertexBufferData[x + V.dataPerVertex * K + I + 7] = r.alphaOffset;
                else
                    for (H = 0; 4 > H;) K = H++, V.vertexBufferData[x + V.dataPerVertex * K + I] = 1, V.vertexBufferData[x + V.dataPerVertex * K + I +
                        1] = 1, V.vertexBufferData[x + V.dataPerVertex * K + I + 2] = 1, V.vertexBufferData[x + V.dataPerVertex * K + I + 3] = 1, V.vertexBufferData[x + V.dataPerVertex * K + I + 4] = 0, V.vertexBufferData[x + V.dataPerVertex * K + I + 5] = 0, V.vertexBufferData[x + V.dataPerVertex * K + I + 6] = 0, V.vertexBufferData[x + V.dataPerVertex * K + I + 7] = 0;
            V.vertexDataPosition += 4 * V.dataPerVertex
        }
    }
    b.__dirty = !1;
    ua.__pool.release(n)
};
V.flush = function(a, b, c) {
    null == V.currentShader && (V.currentShader = b.__defaultDisplayShader);
    if (V.bufferPosition > V.lastFlushedPosition && null != V.currentBitmapData &&
        null != V.currentShader) {
        var d = b.__initDisplayShader(V.currentShader);
        b.setShader(d);
        b.applyBitmapData(V.currentBitmapData, a.smoothing);
        b.applyMatrix(b.__getMatrix(a.__renderTransform, 1));
        a.tileAlphaEnabled ? b.useAlphaArray() : b.applyAlpha(a.__worldAlpha);
        a.tileBlendModeEnabled && b.__setBlendMode(c);
        a.tileColorTransformEnabled ? (b.applyHasColorTransform(!0), b.useColorTransformArray()) : b.applyColorTransform(a.__worldColorTransform);
        b.updateShader();
        c = a.__buffer.vertexBuffer;
        for (var f = V.lastFlushedPosition *
                V.dataPerVertex * 4, h; V.lastFlushedPosition < V.bufferPosition;) {
            h = Math.min(V.bufferPosition - V.lastFlushedPosition, V.context.__quadIndexBufferElements) | 0;
            if (0 >= h) break;
            null != d.__position && V.context.setVertexBufferAt(d.__position.index, c, f, 2);
            null != d.__textureCoord && V.context.setVertexBufferAt(d.__textureCoord.index, c, f + 2, 2);
            a.tileAlphaEnabled && null != d.__alpha && V.context.setVertexBufferAt(d.__alpha.index, c, f + 4, 1);
            if (a.tileColorTransformEnabled) {
                var k = a.tileAlphaEnabled ? 5 : 4;
                null != d.__colorMultiplier && V.context.setVertexBufferAt(d.__colorMultiplier.index,
                    c, f + k, 4);
                null != d.__colorOffset && V.context.setVertexBufferAt(d.__colorOffset.index, c, f + k + 4, 4)
            }
            V.context.drawTriangles(V.context.__quadIndexBuffer, 0, 2 * h);
            V.lastFlushedPosition += h
        }
        b.__clearShader()
    }
    V.lastUsedBitmapData = V.currentBitmapData;
    V.lastUsedShader = V.currentShader
};
V.getRecursiveLength = function(a) {
    a = a.__tiles;
    for (var b = 0, c = 0; c < a.length;) {
        var d = a[c];
        ++c;
        0 < d.__length ? b += V.getRecursiveLength(d) : ++b
    }
    return b
};
V.render = function(a, b) {
    if (a.__renderable && !(0 >= a.__worldAlpha) && (V.context = b.__context3D,
            V.buildBuffer(a, b), 0 != V.numTiles)) {
        V.bufferPosition = 0;
        V.lastFlushedPosition = 0;
        V.lastUsedBitmapData = null;
        V.lastUsedShader = null;
        V.currentBitmapData = null;
        V.currentShader = null;
        V.currentBlendMode = a.__worldBlendMode;
        a.tileBlendModeEnabled || b.__setBlendMode(V.currentBlendMode);
        b.__pushMaskObject(a);
        var c = na.__pool.get();
        c.setTo(0, 0, a.__width, a.__height);
        b.__pushMaskRect(c, a.__renderTransform);
        V.renderTileContainer(a, b, a.__group, a.__worldShader, a.__tileset, a.__worldAlpha, a.tileBlendModeEnabled, V.currentBlendMode,
            null);
        V.flush(a, b, V.currentBlendMode);
        b.__popMaskRect();
        b.__popMaskObject(a);
        na.__pool.release(c)
    }
};
V.renderDrawable = function(a, b) {
    b.__updateCacheBitmap(a, !1);
    if (null == a.__cacheBitmap || a.__isCacheBitmapRender) {
        if (!(null == a.opaqueBackground && null == a.__graphics || !a.__renderable || 0 >= a.__worldAlpha)) {
            if (null != a.opaqueBackground && !a.__isCacheBitmapRender && 0 < a.get_width() && 0 < a.get_height()) {
                b.__setBlendMode(a.__worldBlendMode);
                b.__pushMaskObject(a);
                var c = b.__context3D,
                    d = na.__pool.get();
                d.setTo(0, 0, a.get_width(),
                    a.get_height());
                b.__pushMaskRect(d, a.__renderTransform);
                var f = a.opaqueBackground;
                c.clear((f >>> 16 & 255) / 255, (f >>> 8 & 255) / 255, (f & 255) / 255, 1, 0, 0, 1);
                b.__popMaskRect();
                b.__popMaskObject(a);
                na.__pool.release(d)
            }
            null != a.__graphics && Ee.render(a, b)
        }
        V.render(a, b)
    } else Vd.render(a.__cacheBitmap, b);
    b.__renderEvent(a)
};
V.renderDrawableMask = function(a, b) {
    if (null != a.opaqueBackground || null != a.__graphics) null != a.opaqueBackground && !a.__isCacheBitmapRender && 0 < a.get_width() && a.get_height(), null != a.__graphics && Ee.renderMask(a,
        b);
    V.renderMask(a, b)
};
V.renderTileContainer = function(a, b, c, d, f, h, k, g, p) {
    c = c.__tiles;
    for (var n, q, m, u = null, r, l = 0; l < c.length;) {
        var x = c[l];
        ++l;
        n = null != x.get_tileset() ? x.get_tileset() : f;
        q = x.get_alpha() * h;
        if ((m = x.get_visible()) && !(0 >= q))
            if (m = null != x.get_shader() ? x.get_shader() : d, k && (u = null != x.__blendMode ? x.__blendMode : g), 0 < x.__length) V.renderTileContainer(a, b, x, m, n, q, k, u, p);
            else if (null != n && (r = x.get_id(), q = n.__bitmapData, null != q)) {
            if (-1 == r) {
                if (n = x.__rect, null == n || 0 >= n.width || 0 >= n.height) continue
            } else if (n =
                n.__data[r], null == n) continue;
            (m != V.currentShader || q != V.currentBitmapData && null != V.currentBitmapData || V.currentBlendMode != u) && V.flush(a, b, V.currentBlendMode);
            V.currentBitmapData = q;
            V.currentShader = m;
            V.currentBlendMode = u;
            V.bufferPosition++
        }
    }
};
V.renderMask = function(a, b) {};
V.resizeBuffer = function(a, b) {
    V.numTiles = b;
    null == a.__buffer ? a.__buffer = new Dh(V.context, yj.QUADS, V.numTiles, V.dataPerVertex) : a.__buffer.resize(V.numTiles, V.dataPerVertex);
    V.vertexBufferData = a.__buffer.vertexBufferData
};
var Wd = function() {};
g["openfl.display._internal.Context3DVideo"] = Wd;
Wd.__name__ = "openfl.display._internal.Context3DVideo";
Wd.render = function(a, b) {
    if (a.__renderable && !(0 >= a.__worldAlpha) && null != a.__stream && null != a.__stream.__video) {
        var c = b.__context3D,
            d = c.gl;
        if (null != a.__getTexture(c)) {
            b.__setBlendMode(a.__worldBlendMode);
            b.__pushMaskObject(a);
            var f = b.__initDisplayShader(a.__worldShader);
            b.setShader(f);
            b.applyBitmapData(null, !0, !1);
            b.applyMatrix(b.__getMatrix(a.__renderTransform, 1));
            b.applyAlpha(a.__worldAlpha);
            b.applyColorTransform(a.__worldColorTransform);
            null != f.__textureSize && (Wd.__textureSizeValue[0] = null != a.__stream ? a.__stream.__video.videoWidth : 0, Wd.__textureSizeValue[1] = null != a.__stream ? a.__stream.__video.videoHeight : 0, f.__textureSize.value = Wd.__textureSizeValue);
            b.updateShader();
            c.setTextureAt(0, a.__getTexture(c));
            c.__flushGLTextures();
            d.uniform1i(f.__texture.index, 0);
            a.smoothing ? (d.texParameteri(d.TEXTURE_2D, d.TEXTURE_MAG_FILTER, d.LINEAR), d.texParameteri(d.TEXTURE_2D, d.TEXTURE_MIN_FILTER, d.LINEAR)) : (d.texParameteri(d.TEXTURE_2D, d.TEXTURE_MAG_FILTER,
                d.NEAREST), d.texParameteri(d.TEXTURE_2D, d.TEXTURE_MIN_FILTER, d.NEAREST));
            d = a.__getVertexBuffer(c);
            null != f.__position && c.setVertexBufferAt(f.__position.index, d, 0, 3);
            null != f.__textureCoord && c.setVertexBufferAt(f.__textureCoord.index, d, 3, 2);
            f = a.__getIndexBuffer(c);
            c.drawTriangles(f);
            b.__clearShader();
            b.__popMaskObject(a)
        }
    }
};
Wd.renderDrawable = function(a, b) {
    Wd.render(a, b);
    b.__renderEvent(a)
};
Wd.renderDrawableMask = function(a, b) {
    Wd.renderMask(a, b)
};
Wd.renderMask = function(a, b) {
    if (null != a.__stream && null !=
        a.__stream.__video) {
        var c = b.__context3D,
            d = b.__maskShader;
        b.setShader(d);
        b.applyBitmapData(Xf.opaqueBitmapData, !0);
        b.applyMatrix(b.__getMatrix(a.__renderTransform, 1));
        b.updateShader();
        var f = a.__getVertexBuffer(c);
        null != d.__position && c.setVertexBufferAt(d.__position.index, f, 0, 3);
        null != d.__textureCoord && c.setVertexBufferAt(d.__textureCoord.index, f, 3, 2);
        a = a.__getIndexBuffer(c);
        c.drawTriangles(a);
        b.__clearShader()
    }
};
var Zb = function() {};
g["openfl.display._internal.DOMBitmap"] = Zb;
Zb.__name__ = "openfl.display._internal.DOMBitmap";
Zb.clear = function(a, b) {
    rd.clear(a, b);
    null != a.__image && (b.element.removeChild(a.__image), a.__image = null, a.__style = null);
    null != a.__canvas && (b.element.removeChild(a.__canvas), a.__canvas = null, a.__style = null)
};
Zb.renderCanvas = function(a, b) {
    null != a.__image && (b.element.removeChild(a.__image), a.__image = null);
    null == a.__canvas && (a.__canvas = window.document.createElement("canvas"), a.__context = a.__canvas.getContext("2d"), a.__imageVersion = -1, b.__allowSmoothing && a.smoothing || (a.__context.imageSmoothingEnabled = !1),
        b.__initializeElement(a, a.__canvas));
    a.__imageVersion != a.__bitmapData.image.version && (Ka.convertToCanvas(a.__bitmapData.image), a.__canvas.width = a.__bitmapData.width + 1, a.__canvas.width = a.__bitmapData.width, a.__canvas.height = a.__bitmapData.height, a.__context.drawImage(a.__bitmapData.image.buffer.__srcCanvas, 0, 0), a.__imageVersion = a.__bitmapData.image.version);
    b.__updateClip(a);
    b.__applyStyle(a, !0, !0, !0)
};
Zb.renderDrawable = function(a, b) {
    b.__updateCacheBitmap(a, !1);
    if (null == a.__cacheBitmap || a.__isCacheBitmapRender) {
        null !=
            a.opaqueBackground && !a.__isCacheBitmapRender && 0 < a.get_width() && a.get_height();
        var c = a.__graphics;
        if (null != a.stage && a.__worldVisible && a.__renderable && null != c) {
            z.render(c, b.__canvasRenderer);
            if (c.__softwareDirty || a.__worldAlphaChanged || a.__canvas != c.__canvas) null != c.__canvas ? a.__canvas != c.__canvas && (null != a.__canvas && b.element.removeChild(a.__canvas), a.__canvas = c.__canvas, a.__context = c.__context, b.__initializeElement(a, a.__canvas)) : wf.clear(a, b);
            if (null != a.__canvas) {
                b.__pushMaskObject(a);
                var d = a.__renderTransform;
                a.__renderTransform = c.__worldTransform;
                c.__transformDirty && (c.__transformDirty = !1, a.__renderTransformChanged = !0);
                b.__updateClip(a);
                b.__applyStyle(a, !0, !0, !0);
                a.__renderTransform = d;
                b.__popMaskObject(a)
            }
        } else wf.clear(a, b);
        null != a.stage && a.__worldVisible && a.__renderable && null != a.__bitmapData && a.__bitmapData.__isValid && a.__bitmapData.readable ? (b.__pushMaskObject(a), null != a.__bitmapData.image.buffer.__srcImage ? (d = a.__bitmapData.image.buffer.__srcImage.src, O.startsWith(d, "data:") || O.startsWith(d, "blob:") ?
            Zb.renderCanvas(a, b) : Zb.renderImage(a, b)) : Zb.renderCanvas(a, b), b.__popMaskObject(a)) : Zb.clear(a, b)
    } else b.__renderDrawableClear(a), a.__cacheBitmap.stage = a.stage, c = a.__cacheBitmap, null != c.stage && c.__worldVisible && c.__renderable && null != c.__bitmapData && c.__bitmapData.__isValid && c.__bitmapData.readable ? (b.__pushMaskObject(c), null != c.__bitmapData.image.buffer.__srcImage ? (d = c.__bitmapData.image.buffer.__srcImage.src, O.startsWith(d, "data:") || O.startsWith(d, "blob:") ? Zb.renderCanvas(c, b) : Zb.renderImage(c,
        b)) : Zb.renderCanvas(c, b), b.__popMaskObject(c)) : Zb.clear(c, b);
    b.__renderEvent(a)
};
Zb.renderDrawableClear = function(a, b) {
    Zb.clear(a, b)
};
Zb.renderImage = function(a, b) {
    null != a.__canvas && (b.element.removeChild(a.__canvas), a.__canvas = null);
    null == a.__image && (a.__image = window.document.createElement("img"), a.__image.crossOrigin = "Anonymous", a.__image.src = a.__bitmapData.image.buffer.__srcImage.src, b.__initializeElement(a, a.__image));
    b.__updateClip(a);
    b.__applyStyle(a, !0, !0, !0)
};
var rd = function() {};
g["openfl.display._internal.DOMDisplayObject"] =
    rd;
rd.__name__ = "openfl.display._internal.DOMDisplayObject";
rd.clear = function(a, b) {
    null != a.__cacheBitmap && Zb.clear(a.__cacheBitmap, b);
    wf.clear(a, b)
};
rd.renderDrawable = function(a, b) {
    b.__updateCacheBitmap(a, !1);
    if (null == a.__cacheBitmap || a.__isCacheBitmapRender) {
        null != a.opaqueBackground && !a.__isCacheBitmapRender && 0 < a.get_width() && a.get_height();
        var c = a.__graphics;
        if (null != a.stage && a.__worldVisible && a.__renderable && null != c) {
            z.render(c, b.__canvasRenderer);
            if (c.__softwareDirty || a.__worldAlphaChanged || a.__canvas !=
                c.__canvas) null != c.__canvas ? a.__canvas != c.__canvas && (null != a.__canvas && b.element.removeChild(a.__canvas), a.__canvas = c.__canvas, a.__context = c.__context, b.__initializeElement(a, a.__canvas)) : wf.clear(a, b);
            if (null != a.__canvas) {
                b.__pushMaskObject(a);
                var d = a.__renderTransform;
                a.__renderTransform = c.__worldTransform;
                c.__transformDirty && (c.__transformDirty = !1, a.__renderTransformChanged = !0);
                b.__updateClip(a);
                b.__applyStyle(a, !0, !0, !0);
                a.__renderTransform = d;
                b.__popMaskObject(a)
            }
        } else wf.clear(a, b)
    } else b.__renderDrawableClear(a),
        a.__cacheBitmap.stage = a.stage, c = a.__cacheBitmap, null != c.stage && c.__worldVisible && c.__renderable && null != c.__bitmapData && c.__bitmapData.__isValid && c.__bitmapData.readable ? (b.__pushMaskObject(c), null != c.__bitmapData.image.buffer.__srcImage ? (d = c.__bitmapData.image.buffer.__srcImage.src, O.startsWith(d, "data:") || O.startsWith(d, "blob:") ? Zb.renderCanvas(c, b) : Zb.renderImage(c, b)) : Zb.renderCanvas(c, b), b.__popMaskObject(c)) : Zb.clear(c, b);
    b.__renderEvent(a)
};
rd.renderDrawableClear = function(a, b) {
    rd.clear(a, b)
};
var gj = function() {};
g["openfl.display._internal.DOMDisplayObjectContainer"] = gj;
gj.__name__ = "openfl.display._internal.DOMDisplayObjectContainer";
gj.renderDrawable = function(a, b) {
    for (var c = a.__removedChildren.iterator(); c.hasNext();) {
        var d = c.next();
        null == d.stage && b.__renderDrawable(d)
    }
    for (c = a.__removedChildren.iterator(); c.hasNext();) d = c.next(), null == d.stage && d.__cleanup();
    a.__removedChildren.set_length(0);
    rd.renderDrawable(a, b);
    if (null == a.__cacheBitmap || a.__isCacheBitmapRender) {
        b.__pushMaskObject(a);
        if (null != b.__stage) {
            c = 0;
            for (d = a.__children; c < d.length;) f = d[c], ++c, b.__renderDrawable(f), f.__renderDirty = !1;
            a.__renderDirty = !1
        } else
            for (c = 0, d = a.__children; c < d.length;) f = d[c], ++c, b.__renderDrawable(f);
        b.__popMaskObject(a)
    } else {
        c = 0;
        for (d = a.__children; c < d.length;) {
            var f = d[c];
            ++c;
            b.__renderDrawableClear(f)
        }
        wf.clear(a, b);
        a.__cacheBitmap.stage = a.stage
    }
};
gj.renderDrawableClear = function(a, b) {
    for (var c = a.__removedChildren.iterator(); c.hasNext();) {
        var d = c.next();
        null == d.stage && b.__renderDrawableClear(d)
    }
    for (c =
        a.__removedChildren.iterator(); c.hasNext();) d = c.next(), null == d.stage && d.__cleanup();
    a.__removedChildren.set_length(0);
    c = 0;
    for (d = a.__children; c < d.length;) {
        var f = d[c];
        ++c;
        b.__renderDrawableClear(f)
    }
    rd.clear(a, b)
};
var wf = function() {};
g["openfl.display._internal.DOMShape"] = wf;
wf.__name__ = "openfl.display._internal.DOMShape";
wf.clear = function(a, b) {
    null != a.__canvas && (b.element.removeChild(a.__canvas), a.__canvas = null, a.__style = null)
};
var hj = function() {};
g["openfl.display._internal.DOMSimpleButton"] = hj;
hj.__name__ =
    "openfl.display._internal.DOMSimpleButton";
hj.renderDrawable = function(a, b) {
    b.__pushMaskObject(a);
    for (var c = a.__previousStates.iterator(); c.hasNext();) {
        var d = c.next();
        b.__renderDrawable(d)
    }
    a.__previousStates.set_length(0);
    null != a.__currentState && (a.__currentState.stage != a.stage && a.__currentState.__setStageReference(a.stage), b.__renderDrawable(a.__currentState));
    b.__popMaskObject(a);
    b.__renderEvent(a)
};
hj.renderDrawableClear = function(a, b) {
    rd.renderDrawableClear(a, b)
};
var uf = function() {};
g["openfl.display._internal.DOMTextField"] =
    uf;
uf.__name__ = "openfl.display._internal.DOMTextField";
uf.clear = function(a, b) {
    rd.clear(a, b);
    null != a.__div && (b.element.removeChild(a.__div), a.__div = null, a.__style = null)
};
uf.renderDrawable = function(a, b) {
    a.__domRender = !0;
    b.__updateCacheBitmap(a, a.__forceCachedBitmapUpdate);
    a.__forceCachedBitmapUpdate = !1;
    a.__domRender = !1;
    if (null == a.__cacheBitmap || a.__isCacheBitmapRender) {
        a.__renderedOnCanvasWhileOnDOM && (a.__renderedOnCanvasWhileOnDOM = !1, a.__isHTML && null != a.__htmlText && (a.__updateText(a.__htmlText), a.__dirty = !0, a.__layoutDirty = !0, a.__renderDirty || (a.__renderDirty = !0, a.__setParentRenderDirty())));
        var c = a.__textEngine;
        if (null != a.stage && a.__worldVisible && a.__renderable) {
            if (a.__dirty || a.__renderTransformChanged || null == a.__div)
                if ("" != c.text || c.background || c.border || 1 == c.type) {
                    a.__updateLayout();
                    null == a.__div && (a.__div = window.document.createElement("div"), b.__initializeElement(a, a.__div), a.__style.setProperty("outline", "none", null), a.__div.addEventListener("input", function(b) {
                        b.preventDefault();
                        a.get_htmlText() !=
                            a.__div.innerHTML && (a.set_htmlText(a.__div.innerHTML), a.__dirty = !1, a.dispatchEvent(new Ce("textInput", !1, !1, a.get_htmlText())))
                    }, !0));
                    c.wordWrap ? a.__style.setProperty("word-wrap", "break-word", null) : a.__style.setProperty("white-space", "nowrap", null);
                    a.__style.setProperty("overflow", "hidden", null);
                    c.selectable ? (a.__style.setProperty("cursor", "text", null), a.__style.setProperty("-webkit-user-select", "text", null), a.__style.setProperty("-moz-user-select", "text", null), a.__style.setProperty("-ms-user-select",
                        "text", null), a.__style.setProperty("-o-user-select", "text", null)) : a.__style.setProperty("cursor", "inherit", null);
                    a.__div.contentEditable = 1 == c.type;
                    var d = a.__style;
                    c.background ? d.setProperty("background-color", "#" + O.hex(c.backgroundColor & 16777215, 6), null) : d.removeProperty("background-color");
                    var f = c.width,
                        h = c.height,
                        k = "";
                    a.__isHTML && a.__updateText(La.parse(a.__text, a.get_multiline(), a.__styleSheet, a.__textFormat, a.__textEngine.textFormatRanges));
                    for (var g = -a.get_scrollH(), p = c.layoutGroups.iterator(); p.hasNext();) {
                        var q =
                            p.next();
                        if (!(q.lineIndex < a.get_scrollV() - 1)) {
                            if (q.lineIndex > c.get_bottomScrollV() - 1) break;
                            k += '<div style="';
                            null != q.format.font && (k += "font: " + Nb.getFont(q.format) + "; ");
                            null != q.format.color && (k += "color: #" + O.hex(q.format.color & 16777215, 6) + "; ");
                            1 == q.format.underline && (k += "text-decoration: underline; ");
                            if (null != q.format.align) switch (q.format.align) {
                                case 0:
                                    k += "text-align: center; ";
                                    break;
                                case 2:
                                    k += "text-align: justify; ";
                                    break;
                                case 4:
                                    k += "text-align: right; ";
                                    break;
                                default:
                                    k += "text-align: left; "
                            }
                            null !=
                                q.format.leftMargin && (k += "padding-left: " + 1 * q.format.leftMargin + "px; ");
                            null != q.format.rightMargin && (k += "padding-right: " + 1 * q.format.rightMargin + "px; ");
                            null != q.format.indent && (k += "text-indent: " + 1 * q.format.indent + "px; ");
                            k += "left: " + (q.offsetX + g) + "px; top: " + (q.offsetY + 0 + 3) + 'px; vertical-align: top; position: absolute;">';
                            if (null != q.format.url && "" != q.format.url) {
                                var u = "text-decoration: underline; ";
                                null != q.format.color && (u += "color: #" + O.hex(q.format.color & 16777215, 6) + "; ");
                                k += "<a style='" + u + "' href='" +
                                    q.format.url + "' target='" + q.format.target + "'>"
                            }
                            k = a.__isHTML ? k + O.replace(c.text.substring(q.startIndex, q.endIndex), " ", "&nbsp;") : k + O.replace(O.htmlEscape(c.text.substring(q.startIndex, q.endIndex)), " ", "&nbsp;");
                            null != q.format.url && "" != q.format.url && (k += "</a>");
                            k += "</div>"
                        }
                    }
                    c.border ? (d.setProperty("border", "solid 1px #" + O.hex(c.borderColor & 16777215, 6), null), a.__renderTransform.translate(-1, -1), a.__renderTransformChanged = !0, a.__transformDirty = !0) : "" != d.border && (d.removeProperty("border"), a.__renderTransformChanged = !0);
                    d.setProperty("width", f + "px", null);
                    d.setProperty("height", h + "px", null);
                    a.__div.innerHTML = k;
                    a.__dirty = !1
                } else null != a.__div && (b.element.removeChild(a.__div), a.__div = null);
            null != a.__div && (c = b.__roundPixels, b.__roundPixels = !0, b.__updateClip(a), b.__applyStyle(a, !0, !0, !0), b.__roundPixels = c)
        } else uf.clear(a, b)
    } else b.__renderDrawableClear(a), a.__cacheBitmap.stage = a.stage, c = a.__cacheBitmap, null != c.stage && c.__worldVisible && c.__renderable && null != c.__bitmapData && c.__bitmapData.__isValid && c.__bitmapData.readable ?
        (b.__pushMaskObject(c), null != c.__bitmapData.image.buffer.__srcImage ? (d = c.__bitmapData.image.buffer.__srcImage.src, O.startsWith(d, "data:") || O.startsWith(d, "blob:") ? Zb.renderCanvas(c, b) : Zb.renderImage(c, b)) : Zb.renderCanvas(c, b), b.__popMaskObject(c)) : Zb.clear(c, b);
    b.__renderEvent(a)
};
uf.renderDrawableClear = function(a, b) {
    uf.clear(a, b)
};
var vf = function() {};
g["openfl.display._internal.DOMTilemap"] = vf;
vf.__name__ = "openfl.display._internal.DOMTilemap";
vf.clear = function(a, b) {
    rd.clear(a, b);
    null != a.__canvas &&
        (b.element.removeChild(a.__canvas), a.__canvas = null, a.__style = null)
};
vf.renderDrawable = function(a, b) {
    b.__updateCacheBitmap(a, !1);
    if (null == a.__cacheBitmap || a.__isCacheBitmapRender)
        if (null != a.stage && a.__worldVisible && a.__renderable && 0 < a.__group.__tiles.length) {
            null == a.__canvas && (a.__canvas = window.document.createElement("canvas"), a.__context = a.__canvas.getContext("2d"), b.__initializeElement(a, a.__canvas));
            a.__canvas.width = a.__width;
            a.__canvas.height = a.__height;
            b.__canvasRenderer.context = a.__context;
            var c =
                a.__renderTransform;
            a.__renderTransform = ua.__identity;
            var d = b.__canvasRenderer;
            if ((null != a.opaqueBackground || null != a.__graphics) && a.__renderable) {
                var f = d.__getAlpha(a.__worldAlpha);
                if (!(0 >= f)) {
                    if (null != a.opaqueBackground && !a.__isCacheBitmapRender && 0 < a.get_width() && 0 < a.get_height()) {
                        d.__setBlendMode(a.__worldBlendMode);
                        d.__pushMaskObject(a);
                        var h = d.context;
                        d.setTransform(a.__renderTransform, h);
                        var k = a.opaqueBackground;
                        h.fillStyle = "rgb(" + (k >>> 16 & 255) + "," + (k >>> 8 & 255) + "," + (k & 255) + ")";
                        h.fillRect(0, 0, a.get_width(),
                            a.get_height());
                        d.__popMaskObject(a)
                    }
                    if (null != a.__graphics && a.__renderable && (f = d.__getAlpha(a.__worldAlpha), !(0 >= f))) {
                        var g = a.__graphics;
                        if (null != g) {
                            z.render(g, d);
                            var p = g.__width,
                                q = g.__height;
                            k = g.__canvas;
                            if (null != k && g.__visible && 1 <= p && 1 <= q) {
                                var u = g.__worldTransform;
                                h = d.context;
                                var m = a.__scrollRect,
                                    r = a.__worldScale9Grid;
                                if (null == m || 0 < m.width && 0 < m.height) {
                                    d.__setBlendMode(a.__worldBlendMode);
                                    d.__pushMaskObject(a);
                                    h.globalAlpha = f;
                                    if (null != r && 0 == u.b && 0 == u.c) {
                                        var l = d.__pixelRatio;
                                        f = ua.__pool.get();
                                        f.translate(u.tx,
                                            u.ty);
                                        d.setTransform(f, h);
                                        ua.__pool.release(f);
                                        f = g.__bounds;
                                        var x = g.__renderTransform.a / g.__bitmapScale,
                                            D = g.__renderTransform.d / g.__bitmapScale,
                                            w = x * u.a,
                                            J = D * u.d;
                                        u = Math.max(1, Math.round(r.x * x));
                                        g = Math.round(r.y * D);
                                        m = Math.max(1, Math.round((f.get_right() - r.get_right()) * x));
                                        var y = Math.round((f.get_bottom() - r.get_bottom()) * D);
                                        x = Math.round(r.width * x);
                                        r = Math.round(r.height * D);
                                        D = Math.round(u / l);
                                        var C = Math.round(g / l),
                                            t = Math.round(m / l);
                                        l = Math.round(y / l);
                                        w = f.width * w - D - t;
                                        f = f.height * J - C - l;
                                        d.applySmoothing(h, !1);
                                        0 != x && 0 != r ? (h.drawImage(k, 0, 0, u, g, 0, 0, D, C), h.drawImage(k, u, 0, x, g, D, 0, w, C), h.drawImage(k, u + x, 0, m, g, D + w, 0, t, C), h.drawImage(k, 0, g, u, r, 0, C, D, f), h.drawImage(k, u, g, x, r, D, C, w, f), h.drawImage(k, u + x, g, m, r, D + w, C, t, f), h.drawImage(k, 0, g + r, u, y, 0, C + f, D, l), h.drawImage(k, u, g + r, x, y, D, C + f, w, l), h.drawImage(k, u + x, g + r, m, y, D + w, C + f, t, l)) : 0 == x && 0 != r ? (q = D + w + t, h.drawImage(k, 0, 0, p, g, 0, 0, q, C), h.drawImage(k, 0, g, p, r, 0, C, q, f), h.drawImage(k, 0, g + r, p, y, 0, C + f, q, l)) : 0 == r && 0 != x && (p = C + f + l, h.drawImage(k, 0, 0, u, q, 0, 0, D, p), h.drawImage(k, u,
                                            0, x, q, D, 0, w, p), h.drawImage(k, u + x, 0, m, q, D + w, 0, t, p))
                                    } else d.setTransform(u, h), h.drawImage(k, 0, 0, p, q);
                                    d.__popMaskObject(a)
                                }
                            }
                        }
                    }
                }
            }
            d = b.__canvasRenderer;
            a.__renderable && 0 != a.__group.__tiles.length && (f = d.__getAlpha(a.__worldAlpha), 0 >= f || (h = d.context, d.__setBlendMode(a.__worldBlendMode), d.__pushMaskObject(a), k = na.__pool.get(), k.setTo(0, 0, a.__width, a.__height), d.__pushMaskRect(k, a.__renderTransform), d.__allowSmoothing && a.smoothing || (h.imageSmoothingEnabled = !1), Ze.renderTileContainer(a.__group, d, a.__renderTransform,
                a.__tileset, d.__allowSmoothing && a.smoothing, a.tileAlphaEnabled, f, a.tileBlendModeEnabled, a.__worldBlendMode, null, null, k), d.__allowSmoothing && a.smoothing || (h.imageSmoothingEnabled = !0), d.__popMaskRect(), d.__popMaskObject(a), na.__pool.release(k)));
            a.__renderTransform = c;
            b.__canvasRenderer.context = null;
            b.__updateClip(a);
            b.__applyStyle(a, !0, !1, !0)
        } else vf.clear(a, b);
    else b.__renderDrawableClear(a), a.__cacheBitmap.stage = a.stage, c = a.__cacheBitmap, null != c.stage && c.__worldVisible && c.__renderable && null != c.__bitmapData &&
        c.__bitmapData.__isValid && c.__bitmapData.readable ? (b.__pushMaskObject(c), null != c.__bitmapData.image.buffer.__srcImage ? (d = c.__bitmapData.image.buffer.__srcImage.src, O.startsWith(d, "data:") || O.startsWith(d, "blob:") ? Zb.renderCanvas(c, b) : Zb.renderImage(c, b)) : Zb.renderCanvas(c, b), b.__popMaskObject(c)) : Zb.clear(c, b);
    b.__renderEvent(a)
};
vf.renderDrawableClear = function(a, b) {
    vf.clear(a, b)
};
var $e = function() {};
g["openfl.display._internal.DOMVideo"] = $e;
$e.__name__ = "openfl.display._internal.DOMVideo";
$e.clear =
    function(a, b) {
        rd.clear(a, b);
        a.__active && (b.element.removeChild(a.__stream.__video), a.__active = !1)
    };
$e.render = function(a, b) {
    null != a.stage && null != a.__stream && a.__worldVisible && a.__renderable ? (a.__active || (b.__initializeElement(a, a.__stream.__video), a.__active = !0, a.__dirty = !0), a.__dirty && (a.__stream.__video.width = a.__width | 0, a.__stream.__video.height = a.__height | 0, a.__dirty = !1), b.__updateClip(a), b.__applyStyle(a, !0, !0, !0)) : $e.clear(a, b)
};
$e.renderDrawable = function(a, b) {
    $e.render(a, b);
    b.__renderEvent(a)
};
$e.renderDrawableClear = function(a, b) {
    rd.renderDrawableClear(a, b)
};
var ue = function(a) {
    this.buffer = a;
    this.bPos = this.iPos = this.fPos = this.oPos = this.ffPos = this.iiPos = this.tsPos = 0;
    this.prev = da.UNKNOWN
};
g["openfl.display._internal.DrawCommandReader"] = ue;
ue.__name__ = "openfl.display._internal.DrawCommandReader";
ue.prototype = {
    destroy: function() {
        this.buffer = null;
        this.reset()
    },
    reset: function() {
        this.bPos = this.iPos = this.fPos = this.oPos = this.ffPos = this.iiPos = this.tsPos = 0
    },
    __class__: ue
};
var da = y["openfl.display._internal.DrawCommandType"] = {
    __ename__: "openfl.display._internal.DrawCommandType",
    __constructs__: null,
    BEGIN_BITMAP_FILL: {
        _hx_name: "BEGIN_BITMAP_FILL",
        _hx_index: 0,
        __enum__: "openfl.display._internal.DrawCommandType",
        toString: r
    },
    BEGIN_FILL: {
        _hx_name: "BEGIN_FILL",
        _hx_index: 1,
        __enum__: "openfl.display._internal.DrawCommandType",
        toString: r
    },
    BEGIN_GRADIENT_FILL: {
        _hx_name: "BEGIN_GRADIENT_FILL",
        _hx_index: 2,
        __enum__: "openfl.display._internal.DrawCommandType",
        toString: r
    },
    BEGIN_SHADER_FILL: {
        _hx_name: "BEGIN_SHADER_FILL",
        _hx_index: 3,
        __enum__: "openfl.display._internal.DrawCommandType",
        toString: r
    },
    CUBIC_CURVE_TO: {
        _hx_name: "CUBIC_CURVE_TO",
        _hx_index: 4,
        __enum__: "openfl.display._internal.DrawCommandType",
        toString: r
    },
    CURVE_TO: {
        _hx_name: "CURVE_TO",
        _hx_index: 5,
        __enum__: "openfl.display._internal.DrawCommandType",
        toString: r
    },
    DRAW_CIRCLE: {
        _hx_name: "DRAW_CIRCLE",
        _hx_index: 6,
        __enum__: "openfl.display._internal.DrawCommandType",
        toString: r
    },
    DRAW_ELLIPSE: {
        _hx_name: "DRAW_ELLIPSE",
        _hx_index: 7,
        __enum__: "openfl.display._internal.DrawCommandType",
        toString: r
    },
    DRAW_QUADS: {
        _hx_name: "DRAW_QUADS",
        _hx_index: 8,
        __enum__: "openfl.display._internal.DrawCommandType",
        toString: r
    },
    DRAW_RECT: {
        _hx_name: "DRAW_RECT",
        _hx_index: 9,
        __enum__: "openfl.display._internal.DrawCommandType",
        toString: r
    },
    DRAW_ROUND_RECT: {
        _hx_name: "DRAW_ROUND_RECT",
        _hx_index: 10,
        __enum__: "openfl.display._internal.DrawCommandType",
        toString: r
    },
    DRAW_TILES: {
        _hx_name: "DRAW_TILES",
        _hx_index: 11,
        __enum__: "openfl.display._internal.DrawCommandType",
        toString: r
    },
    DRAW_TRIANGLES: {
        _hx_name: "DRAW_TRIANGLES",
        _hx_index: 12,
        __enum__: "openfl.display._internal.DrawCommandType",
        toString: r
    },
    END_FILL: {
        _hx_name: "END_FILL",
        _hx_index: 13,
        __enum__: "openfl.display._internal.DrawCommandType",
        toString: r
    },
    LINE_BITMAP_STYLE: {
        _hx_name: "LINE_BITMAP_STYLE",
        _hx_index: 14,
        __enum__: "openfl.display._internal.DrawCommandType",
        toString: r
    },
    LINE_GRADIENT_STYLE: {
        _hx_name: "LINE_GRADIENT_STYLE",
        _hx_index: 15,
        __enum__: "openfl.display._internal.DrawCommandType",
        toString: r
    },
    LINE_STYLE: {
        _hx_name: "LINE_STYLE",
        _hx_index: 16,
        __enum__: "openfl.display._internal.DrawCommandType",
        toString: r
    },
    LINE_TO: {
        _hx_name: "LINE_TO",
        _hx_index: 17,
        __enum__: "openfl.display._internal.DrawCommandType",
        toString: r
    },
    MOVE_TO: {
        _hx_name: "MOVE_TO",
        _hx_index: 18,
        __enum__: "openfl.display._internal.DrawCommandType",
        toString: r
    },
    OVERRIDE_BLEND_MODE: {
        _hx_name: "OVERRIDE_BLEND_MODE",
        _hx_index: 19,
        __enum__: "openfl.display._internal.DrawCommandType",
        toString: r
    },
    OVERRIDE_MATRIX: {
        _hx_name: "OVERRIDE_MATRIX",
        _hx_index: 20,
        __enum__: "openfl.display._internal.DrawCommandType",
        toString: r
    },
    WINDING_EVEN_ODD: {
        _hx_name: "WINDING_EVEN_ODD",
        _hx_index: 21,
        __enum__: "openfl.display._internal.DrawCommandType",
        toString: r
    },
    WINDING_NON_ZERO: {
        _hx_name: "WINDING_NON_ZERO",
        _hx_index: 22,
        __enum__: "openfl.display._internal.DrawCommandType",
        toString: r
    },
    UNKNOWN: {
        _hx_name: "UNKNOWN",
        _hx_index: 23,
        __enum__: "openfl.display._internal.DrawCommandType",
        toString: r
    }
};
da.__constructs__ = [da.BEGIN_BITMAP_FILL, da.BEGIN_FILL, da.BEGIN_GRADIENT_FILL, da.BEGIN_SHADER_FILL, da.CUBIC_CURVE_TO, da.CURVE_TO, da.DRAW_CIRCLE, da.DRAW_ELLIPSE, da.DRAW_QUADS, da.DRAW_RECT, da.DRAW_ROUND_RECT, da.DRAW_TILES, da.DRAW_TRIANGLES, da.END_FILL, da.LINE_BITMAP_STYLE,
    da.LINE_GRADIENT_STYLE, da.LINE_STYLE, da.LINE_TO, da.MOVE_TO, da.OVERRIDE_BLEND_MODE, da.OVERRIDE_MATRIX, da.WINDING_EVEN_ODD, da.WINDING_NON_ZERO, da.UNKNOWN
];
var bg = function(a, b, c, d, f, h, k) {
    null == k && (k = !1);
    null == h && (h = !1);
    null == f && (f = !1);
    null == d && (d = 0);
    null == c && (c = 2);
    null == b && (b = 5);
    null == a && (a = 0);
    this.wrap = a;
    this.filter = b;
    this.mipfilter = c;
    this.lodBias = d;
    this.ignoreSampler = f;
    this.centroid = h;
    this.textureAlpha = k
};
g["openfl.display._internal.SamplerState"] = bg;
bg.__name__ = "openfl.display._internal.SamplerState";
bg.prototype = {
    clone: function() {
        var a = new bg(this.wrap, this.filter, this.mipfilter, this.lodBias, this.ignoreSampler, this.centroid, this.textureAlpha);
        a.mipmapGenerated = this.mipmapGenerated;
        return a
    },
    copyFrom: function(a) {
        null == a || a.ignoreSampler || (this.wrap = a.wrap, this.filter = a.filter, this.mipfilter = a.mipfilter, this.lodBias = a.lodBias, this.centroid = a.centroid, this.textureAlpha = a.textureAlpha)
    },
    equals: function(a) {
        return null == a ? !1 : this.wrap == a.wrap && this.filter == a.filter && this.mipfilter == a.mipfilter && this.lodBias ==
            a.lodBias ? this.textureAlpha == a.textureAlpha : !1
    },
    __class__: bg
};
var kj = function() {
    this.inputRefs = [];
    this.inputFilter = [];
    this.inputMipFilter = [];
    this.inputs = [];
    this.inputWrap = [];
    this.overrideIntNames = [];
    this.overrideIntValues = [];
    this.overrideFloatNames = [];
    this.overrideFloatValues = [];
    this.overrideBoolNames = [];
    this.overrideBoolValues = [];
    this.paramLengths = [];
    this.paramPositions = [];
    this.paramRefs_Bool = [];
    this.paramRefs_Float = [];
    this.paramRefs_Int = [];
    this.paramTypes = []
};
g["openfl.display._internal.ShaderBuffer"] =
    kj;
kj.__name__ = "openfl.display._internal.ShaderBuffer";
kj.prototype = {
    addBoolOverride: function(a, b) {
        this.overrideBoolNames[this.overrideBoolCount] = a;
        this.overrideBoolValues[this.overrideBoolCount] = b;
        this.overrideBoolCount++
    },
    addFloatOverride: function(a, b) {
        this.overrideFloatNames[this.overrideFloatCount] = a;
        this.overrideFloatValues[this.overrideFloatCount] = b;
        this.overrideFloatCount++
    },
    clearOverride: function() {
        this.overrideBoolCount = this.overrideFloatCount = this.overrideIntCount = 0
    },
    __class__: kj
};
var ub =
    function(a, b, c) {
        this.driverInfo = "OpenGL (Direct blitting)";
        this.backBufferHeight = this.backBufferWidth = 0;
        oa.call(this);
        this.__stage = a;
        this.__contextState = b;
        this.__stage3D = c;
        this.__context = a.window.context;
        this.gl = this.__context.webgl;
        null == this.__contextState && (this.__contextState = new Eh);
        this.__state = new Eh;
        var d;
        this.__vertexConstants = new Float32Array(512);
        this.__fragmentConstants = new Float32Array(512);
        var f = null;
        a = [1, 1, 1, 1];
        var h = d = c = b = null;
        this.__positionScale = a = null != f ? new Float32Array(f) : null !=
            a ? new Float32Array(a) : null != b ? new Float32Array(b.__array) : null != c ? new Float32Array(c) : null != d ? null == h ? new Float32Array(d, 0) : new Float32Array(d, 0, h) : null;
        this.__programs = new Qa; - 1 == ub.__glMaxViewportDims && (ub.__glMaxViewportDims = this.gl.getParameter(this.gl.MAX_VIEWPORT_DIMS));
        this.maxBackBufferHeight = this.maxBackBufferWidth = ub.__glMaxViewportDims; - 1 == ub.__glMaxTextureMaxAnisotropy && (a = this.gl.getExtension("EXT_texture_filter_anisotropic"), null != a && Object.prototype.hasOwnProperty.call(a, "MAX_TEXTURE_MAX_ANISOTROPY_EXT") ||
            (a = this.gl.getExtension("MOZ_EXT_texture_filter_anisotropic")), null != a && Object.prototype.hasOwnProperty.call(a, "MAX_TEXTURE_MAX_ANISOTROPY_EXT") || (a = this.gl.getExtension("WEBKIT_EXT_texture_filter_anisotropic")), null != a ? (ub.__glTextureMaxAnisotropy = a.TEXTURE_MAX_ANISOTROPY_EXT, ub.__glMaxTextureMaxAnisotropy = this.gl.getParameter(a.MAX_TEXTURE_MAX_ANISOTROPY_EXT)) : (ub.__glTextureMaxAnisotropy = 0, ub.__glMaxTextureMaxAnisotropy = 0)); - 1 == ub.__glDepthStencil && (ub.__glDepthStencil = this.gl.DEPTH_STENCIL); - 1 ==
            ub.__glMemoryTotalAvailable && (a = this.gl.getExtension("NVX_gpu_memory_info"), null != a && (ub.__glMemoryTotalAvailable = a.GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX, ub.__glMemoryCurrentAvailable = a.GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX));
        null == ub.__driverInfo && (a = this.gl.getParameter(this.gl.VENDOR), b = this.gl.getParameter(this.gl.VERSION), c = this.gl.getParameter(this.gl.RENDERER), d = this.gl.getParameter(this.gl.SHADING_LANGUAGE_VERSION), ub.__driverInfo = "OpenGL Vendor=" + a + " Version=" + b + " Renderer=" + c + " GLSL=" +
            d);
        this.driverInfo = ub.__driverInfo;
        this.__quadIndexBufferElements = 16383;
        f = this.__quadIndexBufferCount = 6 * this.__quadIndexBufferElements;
        h = d = c = b = a = null;
        a = null != f ? new Uint16Array(f) : null != a ? new Uint16Array(a) : null != b ? new Uint16Array(b.__array) : null != c ? new Uint16Array(c) : null != d ? null == h ? new Uint16Array(d, 0) : new Uint16Array(d, 0, h) : null;
        d = c = b = 0;
        for (h = this.__quadIndexBufferElements; d < h;) d++, a[b] = c, a[b + 1] = c + 1, a[b + 2] = c + 2, a[b + 3] = c + 2, a[b + 4] = c + 1, a[b + 5] = c + 3, b += 6, c += 4;
        this.__quadIndexBuffer = this.createIndexBuffer(this.__quadIndexBufferCount);
        this.__quadIndexBuffer.uploadFromTypedArray(a)
    };
g["openfl.display3D.Context3D"] = ub;
ub.__name__ = "openfl.display3D.Context3D";
ub.__super__ = oa;
ub.prototype = v(oa.prototype, {
    clear: function(a, b, c, d, f, h, k) {
        null == k && (k = 7);
        null == h && (h = 0);
        null == f && (f = 1);
        null == d && (d = 1);
        null == c && (c = 0);
        null == b && (b = 0);
        null == a && (a = 0);
        this.__flushGLFramebuffer();
        this.__flushGLViewport();
        var g = 0;
        if (0 != (k & 1)) {
            null == this.__state.renderToTexture && (this.__stage.context3D != this || this.__stage.__renderer.__cleared || (this.__stage.__renderer.__cleared = !0), this.__cleared = !0);
            g |= this.gl.COLOR_BUFFER_BIT;
            if (1 != this.__contextState.colorMaskRed || 1 != this.__contextState.colorMaskGreen || 1 != this.__contextState.colorMaskBlue || 1 != this.__contextState.colorMaskAlpha) this.gl.colorMask(!0, !0, !0, !0), this.__contextState.colorMaskRed = !0, this.__contextState.colorMaskGreen = !0, this.__contextState.colorMaskBlue = !0, this.__contextState.colorMaskAlpha = !0;
            this.gl.clearColor(a, b, c, d)
        }
        0 != (k & 2) && (g |= this.gl.DEPTH_BUFFER_BIT, 1 != this.__contextState.depthMask && (this.gl.depthMask(!0),
            this.__contextState.depthMask = !0), this.gl.clearDepth(f));
        0 != (k & 4) && (g |= this.gl.STENCIL_BUFFER_BIT, 255 != this.__contextState.stencilWriteMask && (this.gl.stencilMask(255), this.__contextState.stencilWriteMask = 255), this.gl.clearStencil(h), this.__contextState.stencilWriteMask = 255);
        0 != g && (this.__setGLScissorTest(!1), this.gl.clear(g))
    },
    configureBackBuffer: function(a, b, c, d, f, h) {
        null == h && (h = !1);
        null == f && (f = !1);
        null == d && (d = !0);
        f && (a = a * this.__stage.window.__scale | 0, b = b * this.__stage.window.__scale | 0);
        if (null ==
            this.__stage3D) this.backBufferWidth = a, this.backBufferHeight = b, this.__backBufferAntiAlias = c, this.__state.backBufferEnableDepthAndStencil = d, this.__backBufferWantsBestResolution = f, this.__backBufferWantsBestResolutionOnBrowserZoom = h;
        else {
            if (null == this.__backBufferTexture || this.backBufferWidth != a || this.backBufferHeight != b) {
                null != this.__backBufferTexture && this.__backBufferTexture.dispose();
                null != this.__frontBufferTexture && this.__frontBufferTexture.dispose();
                this.__backBufferTexture = this.createRectangleTexture(a,
                    b, 1, !0);
                this.__frontBufferTexture = this.createRectangleTexture(a, b, 1, !0);
                null == this.__stage3D.__vertexBuffer && (this.__stage3D.__vertexBuffer = this.createVertexBuffer(4, 5));
                var k = f ? a : a * this.__stage.window.__scale | 0,
                    g = f ? b : b * this.__stage.window.__scale | 0;
                k = la.toFloatVector(null, null, null, [k, g, 0, 1, 1, 0, g, 0, 0, 1, k, 0, 0, 1, 0, 0, 0, 0, 0, 0]);
                this.__stage3D.__vertexBuffer.uploadFromVector(k, 0, 20);
                null == this.__stage3D.__indexBuffer && (this.__stage3D.__indexBuffer = this.createIndexBuffer(6), k = la.toIntVector(null, null,
                    null, [0, 1, 2, 2, 1, 3]), this.__stage3D.__indexBuffer.uploadFromVector(k, 0, 6))
            }
            this.backBufferWidth = a;
            this.backBufferHeight = b;
            this.__backBufferAntiAlias = c;
            this.__state.backBufferEnableDepthAndStencil = d;
            this.__backBufferWantsBestResolution = f;
            this.__backBufferWantsBestResolutionOnBrowserZoom = h;
            this.__state.__primaryGLFramebuffer = this.__backBufferTexture.__getGLFramebuffer(d, c, 0);
            this.__frontBufferTexture.__getGLFramebuffer(d, c, 0)
        }
    },
    createIndexBuffer: function(a, b) {
        null == b && (b = 1);
        return new Jk(this, a, b)
    },
    createProgram: function(a) {
        null == a && (a = 0);
        return new Kk(this, a)
    },
    createRectangleTexture: function(a, b, c, d) {
        return new Fh(this, a, b, Ol.toString(c), d)
    },
    createVertexBuffer: function(a, b, c) {
        null == c && (c = 1);
        return new Lk(this, a, b, Al.toString(c))
    },
    drawTriangles: function(a, b, c) {
        null == c && (c = -1);
        null == b && (b = 0);
        null == this.__state.renderToTexture && (this.__stage.context3D != this || this.__stage.__renderer.__cleared ? this.__cleared || this.clear(0, 0, 0, 0, 1, 0, 1) : this.__stage.__renderer.__clear());
        this.__flushGL();
        null != this.__state.program &&
            this.__state.program.__flush();
        c = -1 == c ? a.__numIndices : 3 * c;
        this.__bindGLElementArrayBuffer(a.__id);
        this.gl.drawElements(this.gl.TRIANGLES, c, this.gl.UNSIGNED_SHORT, 2 * b)
    },
    present: function() {
        this.setRenderToBackBuffer();
        if (null != this.__stage3D && null != this.__backBufferTexture) {
            this.__cleared || this.clear(0, 0, 0, 0, 1, 0, 1);
            var a = this.__backBufferTexture;
            this.__backBufferTexture = this.__frontBufferTexture;
            this.__frontBufferTexture = a;
            this.__state.__primaryGLFramebuffer = this.__backBufferTexture.__getGLFramebuffer(this.__state.backBufferEnableDepthAndStencil,
                this.__backBufferAntiAlias, 0);
            this.__cleared = !1
        }
        this.__present = !0
    },
    setBlendFactors: function(a, b) {
        this.setBlendFactorsSeparate(a, b, a, b)
    },
    setBlendFactorsSeparate: function(a, b, c, d) {
        this.__state.blendSourceRGBFactor = a;
        this.__state.blendDestinationRGBFactor = b;
        this.__state.blendSourceAlphaFactor = c;
        this.__state.blendDestinationAlphaFactor = d;
        this.__setGLBlendEquation(this.gl.FUNC_ADD)
    },
    setColorMask: function(a, b, c, d) {
        this.__state.colorMaskRed = a;
        this.__state.colorMaskGreen = b;
        this.__state.colorMaskBlue = c;
        this.__state.colorMaskAlpha =
            d
    },
    setCulling: function(a) {
        this.__state.culling = a
    },
    setDepthTest: function(a, b) {
        this.__state.depthMask = a;
        this.__state.depthCompareMode = b
    },
    setProgram: function(a) {
        this.__state.program = a;
        this.__state.shader = null;
        if (null != a)
            for (var b = 0, c = a.__samplerStates.length; b < c;) {
                var d = b++;
                null == this.__state.samplerStates[d] ? this.__state.samplerStates[d] = a.__samplerStates[d].clone() : this.__state.samplerStates[d].copyFrom(a.__samplerStates[d])
            }
    },
    setProgramConstantsFromMatrix: function(a, b, c, d) {
        null == d && (d = !1);
        if (null !=
            this.__state.program && 1 == this.__state.program.__format) this.__flushGLProgram(), a = new Float32Array(16), a[0] = c.rawData.get(0), a[1] = c.rawData.get(1), a[2] = c.rawData.get(2), a[3] = c.rawData.get(3), a[4] = c.rawData.get(4), a[5] = c.rawData.get(5), a[6] = c.rawData.get(6), a[7] = c.rawData.get(7), a[8] = c.rawData.get(8), a[9] = c.rawData.get(9), a[10] = c.rawData.get(10), a[11] = c.rawData.get(11), a[12] = c.rawData.get(12), a[13] = c.rawData.get(13), a[14] = c.rawData.get(14), a[15] = c.rawData.get(15), Lc.uniformMatrix4fv(this.gl, b, d, a);
        else {
            var f = (a = 1 == a) ? this.__vertexConstants : this.__fragmentConstants;
            c = c.rawData;
            var h = 4 * b;
            d ? (f[h++] = c.get(0), f[h++] = c.get(4), f[h++] = c.get(8), f[h++] = c.get(12), f[h++] = c.get(1), f[h++] = c.get(5), f[h++] = c.get(9), f[h++] = c.get(13), f[h++] = c.get(2), f[h++] = c.get(6), f[h++] = c.get(10), f[h++] = c.get(14), f[h++] = c.get(3), f[h++] = c.get(7), f[h++] = c.get(11)) : (f[h++] = c.get(0), f[h++] = c.get(1), f[h++] = c.get(2), f[h++] = c.get(3), f[h++] = c.get(4), f[h++] = c.get(5), f[h++] = c.get(6), f[h++] = c.get(7), f[h++] = c.get(8), f[h++] = c.get(9), f[h++] =
                c.get(10), f[h++] = c.get(11), f[h++] = c.get(12), f[h++] = c.get(13), f[h++] = c.get(14));
            f[h++] = c.get(15);
            null != this.__state.program && this.__state.program.__markDirty(a, b, 4)
        }
    },
    setRenderToBackBuffer: function() {
        this.__state.renderToTexture = null
    },
    setRenderToTexture: function(a, b, c, d) {
        null == d && (d = 0);
        null == c && (c = 0);
        null == b && (b = !1);
        this.__state.renderToTexture = a;
        this.__state.renderToTextureDepthStencil = b;
        this.__state.renderToTextureAntiAlias = c;
        this.__state.renderToTextureSurfaceSelector = d
    },
    setSamplerStateAt: function(a,
        b, c, d) {
        null == this.__state.samplerStates[a] && (this.__state.samplerStates[a] = new bg);
        a = this.__state.samplerStates[a];
        a.wrap = b;
        a.filter = c;
        a.mipfilter = d
    },
    setScissorRectangle: function(a) {
        null != a ? (this.__state.scissorEnabled = !0, this.__state.scissorRectangle.copyFrom(a)) : this.__state.scissorEnabled = !1
    },
    setStencilActions: function(a, b, c, d, f) {
        null == f && (f = 5);
        null == d && (d = 5);
        null == c && (c = 5);
        null == b && (b = 0);
        null == a && (a = 2);
        this.__state.stencilTriangleFace = a;
        this.__state.stencilCompareMode = b;
        this.__state.stencilPass =
            c;
        this.__state.stencilDepthFail = d;
        this.__state.stencilFail = f
    },
    setStencilReferenceValue: function(a, b, c) {
        null == c && (c = 255);
        null == b && (b = 255);
        this.__state.stencilReferenceValue = a;
        this.__state.stencilReadMask = b;
        this.__state.stencilWriteMask = c
    },
    setTextureAt: function(a, b) {
        this.__state.textures[a] = b
    },
    setVertexBufferAt: function(a, b, c, d) {
        null == d && (d = 4);
        null == c && (c = 0);
        if (!(0 > a))
            if (null == b) this.gl.disableVertexAttribArray(a), this.__bindGLArrayBuffer(null);
            else switch (this.__bindGLArrayBuffer(b.__id), this.gl.enableVertexAttribArray(a),
                c *= 4, d) {
                case 0:
                    this.gl.vertexAttribPointer(a, 4, this.gl.UNSIGNED_BYTE, !0, b.__stride, c);
                    break;
                case 1:
                    this.gl.vertexAttribPointer(a, 1, this.gl.FLOAT, !1, b.__stride, c);
                    break;
                case 2:
                    this.gl.vertexAttribPointer(a, 2, this.gl.FLOAT, !1, b.__stride, c);
                    break;
                case 3:
                    this.gl.vertexAttribPointer(a, 3, this.gl.FLOAT, !1, b.__stride, c);
                    break;
                case 4:
                    this.gl.vertexAttribPointer(a, 4, this.gl.FLOAT, !1, b.__stride, c);
                    break;
                default:
                    throw new Wc;
            }
    },
    __bindGLArrayBuffer: function(a) {
        this.__contextState.__currentGLArrayBuffer != a && (this.gl.bindBuffer(this.gl.ARRAY_BUFFER,
            a), this.__contextState.__currentGLArrayBuffer = a)
    },
    __bindGLElementArrayBuffer: function(a) {
        this.__contextState.__currentGLElementArrayBuffer != a && (this.gl.bindBuffer(this.gl.ELEMENT_ARRAY_BUFFER, a), this.__contextState.__currentGLElementArrayBuffer = a)
    },
    __bindGLFramebuffer: function(a) {
        this.__contextState.__currentGLFramebuffer != a && (this.gl.bindFramebuffer(this.gl.FRAMEBUFFER, a), this.__contextState.__currentGLFramebuffer = a)
    },
    __bindGLTexture2D: function(a) {
        this.gl.bindTexture(this.gl.TEXTURE_2D, a);
        this.__contextState.__currentGLTexture2D =
            a
    },
    __bindGLTextureCubeMap: function(a) {
        this.gl.bindTexture(this.gl.TEXTURE_CUBE_MAP, a);
        this.__contextState.__currentGLTextureCubeMap = a
    },
    __dispose: function() {
        this.driverInfo += " (Disposed)";
        null != this.__stage3D && (this.__stage3D.__indexBuffer = null, this.__stage3D.__vertexBuffer = null, this.__stage3D = this.__stage3D.context3D = null);
        this.__positionScale = this.__frontBufferTexture = this.__fragmentConstants = this.__renderStage3DProgram = this.__context = this.__backBufferTexture = null;
        this.__present = !1;
        this.__vertexConstants =
            this.__stage = this.__quadIndexBuffer = null
    },
    __drawTriangles: function(a, b) {
        null == a && (a = 0);
        null == this.__state.renderToTexture && (this.__stage.context3D != this || this.__stage.__renderer.__cleared ? this.__cleared || this.clear(0, 0, 0, 0, 1, 0, 1) : this.__stage.__renderer.__clear());
        this.__flushGL();
        null != this.__state.program && this.__state.program.__flush();
        this.gl.drawArrays(this.gl.TRIANGLES, a, b)
    },
    __flushGL: function() {
        this.__flushGLProgram();
        this.__flushGLFramebuffer();
        this.__flushGLViewport();
        this.__flushGLBlend();
        if (this.__contextState.colorMaskRed != this.__state.colorMaskRed || this.__contextState.colorMaskGreen != this.__state.colorMaskGreen || this.__contextState.colorMaskBlue != this.__state.colorMaskBlue || this.__contextState.colorMaskAlpha != this.__state.colorMaskAlpha) this.gl.colorMask(this.__state.colorMaskRed, this.__state.colorMaskGreen, this.__state.colorMaskBlue, this.__state.colorMaskAlpha), this.__contextState.colorMaskRed = this.__state.colorMaskRed, this.__contextState.colorMaskGreen = this.__state.colorMaskGreen,
            this.__contextState.colorMaskBlue = this.__state.colorMaskBlue, this.__contextState.colorMaskAlpha = this.__state.colorMaskAlpha;
        this.__flushGLCulling();
        this.__flushGLDepth();
        this.__flushGLScissor();
        this.__flushGLStencil();
        this.__flushGLTextures()
    },
    __flushGLBlend: function() {
        if (this.__contextState.blendDestinationRGBFactor != this.__state.blendDestinationRGBFactor || this.__contextState.blendSourceRGBFactor != this.__state.blendSourceRGBFactor || this.__contextState.blendDestinationAlphaFactor != this.__state.blendDestinationAlphaFactor ||
            this.__contextState.blendSourceAlphaFactor != this.__state.blendSourceAlphaFactor) this.__setGLBlend(!0), this.__state.blendDestinationRGBFactor == this.__state.blendDestinationAlphaFactor && this.__state.blendSourceRGBFactor == this.__state.blendSourceAlphaFactor ? this.gl.blendFunc(this.__getGLBlend(this.__state.blendSourceRGBFactor), this.__getGLBlend(this.__state.blendDestinationRGBFactor)) : this.gl.blendFuncSeparate(this.__getGLBlend(this.__state.blendSourceRGBFactor), this.__getGLBlend(this.__state.blendDestinationRGBFactor),
            this.__getGLBlend(this.__state.blendSourceAlphaFactor), this.__getGLBlend(this.__state.blendDestinationAlphaFactor)), this.__contextState.blendDestinationRGBFactor = this.__state.blendDestinationRGBFactor, this.__contextState.blendSourceRGBFactor = this.__state.blendSourceRGBFactor, this.__contextState.blendDestinationAlphaFactor = this.__state.blendDestinationAlphaFactor, this.__contextState.blendSourceAlphaFactor = this.__state.blendSourceAlphaFactor
    },
    __flushGLCulling: function() {
        if (this.__contextState.culling !=
            this.__state.culling) {
            if (3 == this.__state.culling) this.__setGLCullFace(!1);
            else switch (this.__setGLCullFace(!0), this.__state.culling) {
                case 0:
                    this.gl.cullFace(this.gl.BACK);
                    break;
                case 1:
                    this.gl.cullFace(this.gl.FRONT);
                    break;
                case 2:
                    this.gl.cullFace(this.gl.FRONT_AND_BACK);
                    break;
                case 3:
                    break;
                default:
                    throw new Wc;
            }
            this.__contextState.culling = this.__state.culling
        }
    },
    __flushGLDepth: function() {
        var a = this.__state.depthMask && (null != this.__state.renderToTexture ? this.__state.renderToTextureDepthStencil : this.__state.backBufferEnableDepthAndStencil);
        this.__contextState.depthMask != a && (this.gl.depthMask(a), this.__contextState.depthMask = a);
        if (this.__contextState.depthCompareMode != this.__state.depthCompareMode) {
            switch (this.__state.depthCompareMode) {
                case 0:
                    this.gl.depthFunc(this.gl.ALWAYS);
                    break;
                case 1:
                    this.gl.depthFunc(this.gl.EQUAL);
                    break;
                case 2:
                    this.gl.depthFunc(this.gl.GREATER);
                    break;
                case 3:
                    this.gl.depthFunc(this.gl.GEQUAL);
                    break;
                case 4:
                    this.gl.depthFunc(this.gl.LESS);
                    break;
                case 5:
                    this.gl.depthFunc(this.gl.LEQUAL);
                    break;
                case 6:
                    this.gl.depthFunc(this.gl.NEVER);
                    break;
                case 7:
                    this.gl.depthFunc(this.gl.NOTEQUAL);
                    break;
                default:
                    throw new Wc;
            }
            this.__contextState.depthCompareMode = this.__state.depthCompareMode
        }
    },
    __flushGLFramebuffer: function() {
        if (null != this.__state.renderToTexture) {
            if (this.__contextState.renderToTexture != this.__state.renderToTexture || this.__contextState.renderToTextureSurfaceSelector != this.__state.renderToTextureSurfaceSelector) {
                var a = this.__state.renderToTexture.__getGLFramebuffer(this.__state.renderToTextureDepthStencil, this.__state.renderToTextureAntiAlias,
                    this.__state.renderToTextureSurfaceSelector);
                this.__bindGLFramebuffer(a);
                this.__contextState.renderToTexture = this.__state.renderToTexture;
                this.__contextState.renderToTextureAntiAlias = this.__state.renderToTextureAntiAlias;
                this.__contextState.renderToTextureDepthStencil = this.__state.renderToTextureDepthStencil;
                this.__contextState.renderToTextureSurfaceSelector = this.__state.renderToTextureSurfaceSelector
            }
            this.__setGLDepthTest(this.__state.renderToTextureDepthStencil);
            this.__setGLStencilTest(this.__state.renderToTextureDepthStencil);
            this.__setGLFrontFace(!0)
        } else {
            if (null == this.__stage && 0 == this.backBufferWidth && 0 == this.backBufferHeight) throw new Vb("Context3D backbuffer has not been configured");
            if (null != this.__contextState.renderToTexture || this.__contextState.__currentGLFramebuffer != this.__state.__primaryGLFramebuffer || this.__contextState.backBufferEnableDepthAndStencil != this.__state.backBufferEnableDepthAndStencil) this.__bindGLFramebuffer(this.__state.__primaryGLFramebuffer), this.__contextState.renderToTexture = null, this.__contextState.backBufferEnableDepthAndStencil =
                this.__state.backBufferEnableDepthAndStencil;
            this.__setGLDepthTest(this.__state.backBufferEnableDepthAndStencil);
            this.__setGLStencilTest(this.__state.backBufferEnableDepthAndStencil);
            this.__setGLFrontFace(this.__stage.context3D != this)
        }
    },
    __flushGLProgram: function() {
        var a = this.__state.shader,
            b = this.__state.program;
        this.__contextState.shader != a && (null != this.__contextState.shader && this.__contextState.shader.__disable(), null != a && a.__enable(), this.__contextState.shader = a);
        this.__contextState.program != b &&
            (null != this.__contextState.program && this.__contextState.program.__disable(), null != b && b.__enable(), this.__contextState.program = b);
        null != b && 0 == b.__format && (this.__positionScale[1] = this.__stage.context3D == this && null == this.__state.renderToTexture ? 1 : -1, b.__setPositionScale(this.__positionScale))
    },
    __flushGLScissor: function() {
        if (this.__state.scissorEnabled) {
            this.__setGLScissorTest(!0);
            this.__contextState.scissorEnabled = !0;
            var a = this.__state.scissorRectangle.x | 0,
                b = this.__state.scissorRectangle.y | 0,
                c = this.__state.scissorRectangle.width |
                0,
                d = this.__state.scissorRectangle.height | 0;
            this.__backBufferWantsBestResolution && (a = this.__state.scissorRectangle.x * this.__stage.window.__scale | 0, b = this.__state.scissorRectangle.y * this.__stage.window.__scale | 0, c = this.__state.scissorRectangle.width * this.__stage.window.__scale | 0, d = this.__state.scissorRectangle.height * this.__stage.window.__scale | 0);
            null == this.__state.renderToTexture && null == this.__stage3D && (b = (this.__stage.window.__height * this.__stage.window.__scale | 0) - d - b);
            if (this.__contextState.scissorRectangle.x !=
                a || this.__contextState.scissorRectangle.y != b || this.__contextState.scissorRectangle.width != c || this.__contextState.scissorRectangle.height != d) this.gl.scissor(a, b, c, d), this.__contextState.scissorRectangle.setTo(a, b, c, d)
        } else this.__contextState.scissorEnabled != this.__state.scissorEnabled && (this.__setGLScissorTest(!1), this.__contextState.scissorEnabled = !1)
    },
    __flushGLStencil: function() {
        if (this.__contextState.stencilTriangleFace != this.__state.stencilTriangleFace || this.__contextState.stencilPass != this.__state.stencilPass ||
            this.__contextState.stencilDepthFail != this.__state.stencilDepthFail || this.__contextState.stencilFail != this.__state.stencilFail) this.gl.stencilOpSeparate(this.__getGLTriangleFace(this.__state.stencilTriangleFace), this.__getGLStencilAction(this.__state.stencilFail), this.__getGLStencilAction(this.__state.stencilDepthFail), this.__getGLStencilAction(this.__state.stencilPass)), this.__contextState.stencilTriangleFace = this.__state.stencilTriangleFace, this.__contextState.stencilPass = this.__state.stencilPass,
            this.__contextState.stencilDepthFail = this.__state.stencilDepthFail, this.__contextState.stencilFail = this.__state.stencilFail;
        this.__contextState.stencilWriteMask != this.__state.stencilWriteMask && (this.gl.stencilMask(this.__state.stencilWriteMask), this.__contextState.stencilWriteMask = this.__state.stencilWriteMask);
        if (this.__contextState.stencilCompareMode != this.__state.stencilCompareMode || this.__contextState.stencilReferenceValue != this.__state.stencilReferenceValue || this.__contextState.stencilReadMask !=
            this.__state.stencilReadMask) this.gl.stencilFunc(this.__getGLCompareMode(this.__state.stencilCompareMode), this.__state.stencilReferenceValue, this.__state.stencilReadMask), this.__contextState.stencilCompareMode = this.__state.stencilCompareMode, this.__contextState.stencilReferenceValue = this.__state.stencilReferenceValue, this.__contextState.stencilReadMask = this.__state.stencilReadMask
    },
    __flushGLTextures: function() {
        for (var a = 0, b, c, d = 0, f = this.__state.textures.length; d < f;) {
            var h = d++;
            b = this.__state.textures[h];
            c = this.__state.samplerStates[h];
            null == c && (this.__state.samplerStates[h] = new bg, c = this.__state.samplerStates[h]);
            this.gl.activeTexture(this.gl.TEXTURE0 + a);
            null != b ? (b.__textureTarget == this.gl.TEXTURE_2D ? this.__bindGLTexture2D(b.__getTexture()) : this.__bindGLTextureCubeMap(b.__getTexture()), this.__contextState.textures[h] = b, b.__setSamplerState(c)) : this.__bindGLTexture2D(null);
            null != this.__state.program && 0 == this.__state.program.__format && c.textureAlpha && (this.gl.activeTexture(this.gl.TEXTURE0 + a + 4), null !=
                b && null != b.__alphaTexture ? (b.__alphaTexture.__textureTarget == this.gl.TEXTURE_2D ? this.__bindGLTexture2D(b.__alphaTexture.__getTexture()) : this.__bindGLTextureCubeMap(b.__alphaTexture.__getTexture()), b.__alphaTexture.__setSamplerState(c), this.gl.uniform1i(this.__state.program.__agalAlphaSamplerEnabled[a].location, 1)) : (this.__bindGLTexture2D(null), null != this.__state.program.__agalAlphaSamplerEnabled[a] && this.gl.uniform1i(this.__state.program.__agalAlphaSamplerEnabled[a].location, 0)));
            ++a
        }
    },
    __flushGLViewport: function() {
        if (null ==
            this.__state.renderToTexture)
            if (this.__stage.context3D == this) {
                var a = this.backBufferWidth,
                    b = this.backBufferHeight;
                null != this.__stage3D || this.__backBufferWantsBestResolution || (a = this.backBufferWidth * this.__stage.window.__scale | 0, b = this.backBufferHeight * this.__stage.window.__scale | 0);
                var c = null == this.__stage3D ? 0 : this.__stage3D.get_x() | 0,
                    d = this.__stage.window.__height * this.__stage.window.__scale - b - (null == this.__stage3D ? 0 : this.__stage3D.get_y()) | 0;
                this.gl.viewport(c, d, a, b)
            } else this.gl.viewport(0, 0, this.backBufferWidth,
                this.backBufferHeight);
        else b = a = 0, this.__state.renderToTexture instanceof zj ? (b = this.__state.renderToTexture, a = b.__width, b = b.__height) : this.__state.renderToTexture instanceof Fh ? (b = this.__state.renderToTexture, a = b.__width, b = b.__height) : this.__state.renderToTexture instanceof Aj && (b = this.__state.renderToTexture, b = a = b.__size), this.gl.viewport(0, 0, a, b)
    },
    __getGLBlend: function(a) {
        switch (a) {
            case 0:
                return this.gl.DST_ALPHA;
            case 1:
                return this.gl.DST_COLOR;
            case 2:
                return this.gl.ONE;
            case 3:
                return this.gl.ONE_MINUS_DST_ALPHA;
            case 4:
                return this.gl.ONE_MINUS_DST_COLOR;
            case 5:
                return this.gl.ONE_MINUS_SRC_ALPHA;
            case 6:
                return this.gl.ONE_MINUS_SRC_COLOR;
            case 7:
                return this.gl.SRC_ALPHA;
            case 8:
                return this.gl.SRC_COLOR;
            case 9:
                return this.gl.ZERO;
            default:
                throw new Wc;
        }
    },
    __getGLCompareMode: function(a) {
        switch (a) {
            case 0:
                return this.gl.ALWAYS;
            case 1:
                return this.gl.EQUAL;
            case 2:
                return this.gl.GREATER;
            case 3:
                return this.gl.GEQUAL;
            case 4:
                return this.gl.LESS;
            case 5:
                return this.gl.LEQUAL;
            case 6:
                return this.gl.NEVER;
            case 7:
                return this.gl.NOTEQUAL;
            default:
                return this.gl.EQUAL
        }
    },
    __getGLStencilAction: function(a) {
        switch (a) {
            case 0:
                return this.gl.DECR;
            case 1:
                return this.gl.DECR_WRAP;
            case 2:
                return this.gl.INCR;
            case 3:
                return this.gl.INCR_WRAP;
            case 4:
                return this.gl.INVERT;
            case 5:
                return this.gl.KEEP;
            case 6:
                return this.gl.REPLACE;
            case 7:
                return this.gl.ZERO;
            default:
                return this.gl.KEEP
        }
    },
    __getGLTriangleFace: function(a) {
        switch (a) {
            case 0:
                return this.gl.BACK;
            case 1:
                return this.gl.FRONT;
            case 2:
                return this.gl.FRONT_AND_BACK;
            case 3:
                return this.gl.NONE;
            default:
                return this.gl.FRONT_AND_BACK
        }
    },
    __renderStage3D: function(a) {
        var b = a.context3D;
        if (null != b && b != this && null != b.__frontBufferTexture && a.visible && 0 < this.backBufferHeight && 0 < this.backBufferWidth) {
            if (null == this.__renderStage3DProgram) {
                var c = new ma;
                c.assemble(Bl.toString(1), "m44 op, va0, vc0\nmov v0, va1");
                var d = new ma;
                d.assemble(Bl.toString(0), "tex ft1, v0, fs0 <2d,nearest,nomip>\nmov oc, ft1");
                this.__renderStage3DProgram = this.createProgram();
                this.__renderStage3DProgram.upload(c.agalcode, d.agalcode)
            }
            this.setProgram(this.__renderStage3DProgram);
            this.setBlendFactors(2, 9);
            this.setColorMask(!0, !0, !0, !0);
            this.setCulling(3);
            this.setDepthTest(!1, 0);
            this.setStencilActions();
            this.setStencilReferenceValue(0, 0, 0);
            this.setScissorRectangle(null);
            this.setTextureAt(0, b.__frontBufferTexture);
            this.setVertexBufferAt(0, a.__vertexBuffer, 0, 3);
            this.setVertexBufferAt(1, a.__vertexBuffer, 3, 2);
            this.setProgramConstantsFromMatrix(1, 0, a.__renderTransform, !0);
            this.drawTriangles(a.__indexBuffer);
            this.__present = !0
        }
    },
    __setGLBlend: function(a) {
        this.__contextState.__enableGLBlend !=
            a && (a ? this.gl.enable(this.gl.BLEND) : this.gl.disable(this.gl.BLEND), this.__contextState.__enableGLBlend = a)
    },
    __setGLBlendEquation: function(a) {
        this.__contextState.__glBlendEquation != a && (this.gl.blendEquation(a), this.__contextState.__glBlendEquation = a)
    },
    __setGLCullFace: function(a) {
        this.__contextState.__enableGLCullFace != a && (a ? this.gl.enable(this.gl.CULL_FACE) : this.gl.disable(this.gl.CULL_FACE), this.__contextState.__enableGLCullFace = a)
    },
    __setGLDepthTest: function(a) {
        this.__contextState.__enableGLDepthTest !=
            a && (a ? this.gl.enable(this.gl.DEPTH_TEST) : this.gl.disable(this.gl.DEPTH_TEST), this.__contextState.__enableGLDepthTest = a)
    },
    __setGLFrontFace: function(a) {
        this.__contextState.__frontFaceGLCCW != a && (this.gl.frontFace(a ? this.gl.CCW : this.gl.CW), this.__contextState.__frontFaceGLCCW = a)
    },
    __setGLScissorTest: function(a) {
        this.__contextState.__enableGLScissorTest != a && (a ? this.gl.enable(this.gl.SCISSOR_TEST) : this.gl.disable(this.gl.SCISSOR_TEST), this.__contextState.__enableGLScissorTest = a)
    },
    __setGLStencilTest: function(a) {
        this.__contextState.__enableGLStencilTest !=
            a && (a ? this.gl.enable(this.gl.STENCIL_TEST) : this.gl.disable(this.gl.STENCIL_TEST), this.__contextState.__enableGLStencilTest = a)
    },
    __class__: ub
});
var Al = {
        fromString: function(a) {
            switch (a) {
                case "dynamicDraw":
                    return 0;
                case "staticDraw":
                    return 1;
                default:
                    return null
            }
        },
        toString: function(a) {
            switch (a) {
                case 0:
                    return "dynamicDraw";
                case 1:
                    return "staticDraw";
                default:
                    return null
            }
        }
    },
    Bl = {
        toString: function(a) {
            switch (a) {
                case 0:
                    return "fragment";
                case 1:
                    return "vertex";
                default:
                    return null
            }
        }
    },
    Ol = {
        toString: function(a) {
            switch (a) {
                case 0:
                    return "bgrPacked565";
                case 1:
                    return "bgra";
                case 2:
                    return "bgraPacked4444";
                case 3:
                    return "compressed";
                case 4:
                    return "compressedAlpha";
                case 5:
                    return "rgbaHalfFloat";
                default:
                    return null
            }
        }
    },
    Jk = function(a, b, c) {
        this.__context = a;
        this.__numIndices = b;
        a = this.__context.gl;
        this.__id = a.createBuffer();
        this.__usage = 0 == c ? a.DYNAMIC_DRAW : a.STATIC_DRAW
    };
g["openfl.display3D.IndexBuffer3D"] = Jk;
Jk.__name__ = "openfl.display3D.IndexBuffer3D";
Jk.prototype = {
    uploadFromTypedArray: function(a, b) {
        null != a && (b = this.__context.gl, this.__context.__bindGLElementArrayBuffer(this.__id),
            Lc.bufferData(b, b.ELEMENT_ARRAY_BUFFER, a, this.__usage))
    },
    uploadFromVector: function(a, b, c) {
        if (null != a) {
            var d = b + c,
                f = this.__tempUInt16Array;
            if (null == this.__tempUInt16Array || this.__tempUInt16Array.length < c) this.__tempUInt16Array = null != c ? new Uint16Array(c) : null, null != f && this.__tempUInt16Array.set(f);
            for (c = b; c < d;) f = c++, this.__tempUInt16Array[f - b] = a.get(f);
            this.uploadFromTypedArray(this.__tempUInt16Array)
        }
    },
    __class__: Jk
};
var Kk = function(a, b) {
    this.__context = a;
    this.__format = b;
    0 == this.__format ? (this.__agalSamplerUsageMask =
        0, this.__agalUniforms = new ab, this.__agalSamplerUniforms = new ab, this.__agalAlphaSamplerUniforms = new ab, this.__agalAlphaSamplerEnabled = []) : (this.__glslAttribNames = [], this.__glslAttribTypes = [], this.__glslSamplerNames = [], this.__glslUniformLocations = [], this.__glslUniformNames = [], this.__glslUniformTypes = []);
    this.__samplerStates = []
};
g["openfl.display3D.Program3D"] = Kk;
Kk.__name__ = "openfl.display3D.Program3D";
Kk.prototype = {
    upload: function(a, b) {
        if (0 == this.__format) {
            var c = [];
            a = bd.convertToGLSL(a, null);
            b = bd.convertToGLSL(b,
                c);
            5 == Ga.level && (Ga.info(a, {
                fileName: "openfl/display3D/Program3D.hx",
                lineNumber: 399,
                className: "openfl.display3D.Program3D",
                methodName: "upload"
            }), Ga.info(b, {
                fileName: "openfl/display3D/Program3D.hx",
                lineNumber: 400,
                className: "openfl.display3D.Program3D",
                methodName: "upload"
            }));
            this.__deleteShaders();
            this.__uploadFromGLSL(a, b);
            this.__buildAGALUniformList();
            b = 0;
            for (a = c.length; b < a;) {
                var d = b++;
                this.__samplerStates[d] = c[d]
            }
        }
    },
    __buildAGALUniformList: function() {
        if (1 != this.__format) {
            var a = this.__context.gl;
            this.__agalUniforms.clear();
            this.__agalSamplerUniforms.clear();
            this.__agalAlphaSamplerUniforms.clear();
            this.__agalAlphaSamplerEnabled = [];
            this.__agalSamplerUsageMask = 0;
            var b = a.getProgramParameter(this.__glProgram, a.ACTIVE_UNIFORMS);
            for (var c = new ab, d = new ab, f = 0; f < b;) {
                var h = f++,
                    k = a.getActiveUniform(this.__glProgram, h),
                    g = k.name,
                    p = k.size,
                    q = k.type;
                k = new Bj(this.__context);
                k.name = g;
                k.size = p;
                k.type = q;
                k.location = a.getUniformLocation(this.__glProgram, k.name);
                g = k.name.indexOf("[");
                0 <= g && (k.name = k.name.substring(0, g));
                switch (k.type) {
                    case 35674:
                        k.regCount =
                            2;
                        break;
                    case 35675:
                        k.regCount = 3;
                        break;
                    case 35676:
                        k.regCount = 4;
                        break;
                    default:
                        k.regCount = 1
                }
                k.regCount *= k.size;
                this.__agalUniforms.add(k);
                if ("vcPositionScale" == k.name) this.__agalPositionScale = k;
                else if (O.startsWith(k.name, "vc")) k.regIndex = H.parseInt(k.name.substring(2)), k.regData = this.__context.__vertexConstants, c.add(k);
                else if (O.startsWith(k.name, "fc")) k.regIndex = H.parseInt(k.name.substring(2)), k.regData = this.__context.__fragmentConstants, d.add(k);
                else if (O.startsWith(k.name, "sampler") && -1 == k.name.indexOf("alpha"))
                    for (k.regIndex =
                        H.parseInt(k.name.substring(7)), this.__agalSamplerUniforms.add(k), g = 0, p = k.regCount; g < p;) q = g++, this.__agalSamplerUsageMask |= 1 << k.regIndex + q;
                else O.startsWith(k.name, "sampler") && O.endsWith(k.name, "_alpha") ? (g = k.name.indexOf("_") - 7, k.regIndex = H.parseInt(k.name.substring(7, 7 + g)) + 4, this.__agalAlphaSamplerUniforms.add(k)) : O.startsWith(k.name, "sampler") && O.endsWith(k.name, "_alphaEnabled") && (k.regIndex = H.parseInt(k.name.substring(7)), this.__agalAlphaSamplerEnabled[k.regIndex] = k);
                5 == Ga.level && Ga.verbose("" +
                    h + " name:" + k.name + " type:" + k.type + " size:" + k.size + " location:" + H.string(k.location), {
                        fileName: "openfl/display3D/Program3D.hx",
                        lineNumber: 577,
                        className: "openfl.display3D.Program3D",
                        methodName: "__buildAGALUniformList"
                    })
            }
            this.__agalVertexUniformMap = new Gh(F.array(c));
            this.__agalFragmentUniformMap = new Gh(F.array(d))
        }
    },
    __deleteShaders: function() {
        var a = this.__context.gl;
        null != this.__glProgram && (this.__glProgram = null);
        null != this.__glVertexShader && (a.deleteShader(this.__glVertexShader), this.__glVertexShader =
            null);
        null != this.__glFragmentShader && (a.deleteShader(this.__glFragmentShader), this.__glFragmentShader = null)
    },
    __disable: function() {},
    __enable: function() {
        var a = this.__context.gl;
        a.useProgram(this.__glProgram);
        if (0 == this.__format) {
            this.__agalVertexUniformMap.markAllDirty();
            this.__agalFragmentUniformMap.markAllDirty();
            for (var b = this.__agalSamplerUniforms.h; null != b;) {
                var c = b.item;
                b = b.next;
                if (1 == c.regCount) a.uniform1i(c.location, c.regIndex);
                else throw new Wc("!!! TODO: uniform location on webgl");
            }
            for (b =
                this.__agalAlphaSamplerUniforms.h; null != b;)
                if (c = b.item, b = b.next, 1 == c.regCount) a.uniform1i(c.location, c.regIndex);
                else throw new Wc("!!! TODO: uniform location on webgl");
        }
    },
    __flush: function() {
        0 == this.__format && (this.__agalVertexUniformMap.flush(), this.__agalFragmentUniformMap.flush())
    },
    __markDirty: function(a, b, c) {
        1 != this.__format && (a ? this.__agalVertexUniformMap.markDirty(b, c) : this.__agalFragmentUniformMap.markDirty(b, c))
    },
    __setPositionScale: function(a) {
        1 != this.__format && null != this.__agalPositionScale &&
            this.__context.gl.uniform4fv(this.__agalPositionScale.location, a)
    },
    __uploadFromGLSL: function(a, b) {
        var c = this.__context.gl;
        this.__glVertexSource = a;
        this.__glFragmentSource = b;
        this.__glVertexShader = c.createShader(c.VERTEX_SHADER);
        c.shaderSource(this.__glVertexShader, a);
        c.compileShader(this.__glVertexShader);
        if (0 == c.getShaderParameter(this.__glVertexShader, c.COMPILE_STATUS)) {
            var d = "Error compiling vertex shader\n" + c.getShaderInfoLog(this.__glVertexShader);
            Ga.error(d + ("\n" + a), {
                fileName: "openfl/display3D/Program3D.hx",
                lineNumber: 869,
                className: "openfl.display3D.Program3D",
                methodName: "__uploadFromGLSL"
            })
        }
        this.__glFragmentShader = c.createShader(c.FRAGMENT_SHADER);
        c.shaderSource(this.__glFragmentShader, b);
        c.compileShader(this.__glFragmentShader);
        0 == c.getShaderParameter(this.__glFragmentShader, c.COMPILE_STATUS) && (d = "Error compiling fragment shader\n" + c.getShaderInfoLog(this.__glFragmentShader), Ga.error(d + ("\n" + b), {
            fileName: "openfl/display3D/Program3D.hx",
            lineNumber: 881,
            className: "openfl.display3D.Program3D",
            methodName: "__uploadFromGLSL"
        }));
        this.__glProgram = c.createProgram();
        if (0 == this.__format)
            for (b = 0; 16 > b;) {
                var f = b++;
                d = "va" + f; - 1 != a.indexOf(" " + d) && c.bindAttribLocation(this.__glProgram, f, d)
            } else
                for (b = 0, a = this.__glslAttribNames; b < a.length;)
                    if (d = a[b], ++b, -1 < d.indexOf("Position") && O.startsWith(d, "openfl_")) {
                        c.bindAttribLocation(this.__glProgram, 0, d);
                        break
                    } c.attachShader(this.__glProgram, this.__glVertexShader);
        c.attachShader(this.__glProgram, this.__glFragmentShader);
        c.linkProgram(this.__glProgram);
        0 == c.getProgramParameter(this.__glProgram,
            c.LINK_STATUS) && (d = "Unable to initialize the shader program\n" + c.getProgramInfoLog(this.__glProgram), Ga.error(d, {
            fileName: "openfl/display3D/Program3D.hx",
            lineNumber: 922,
            className: "openfl.display3D.Program3D",
            methodName: "__uploadFromGLSL"
        }))
    },
    __class__: Kk
};
var Bj = function(a) {
    this.context = a;
    this.isDirty = !0;
    this.regDataPointer = new Ek(null, 0)
};
g["openfl.display3D.Uniform"] = Bj;
Bj.__name__ = "openfl.display3D.Uniform";
Bj.prototype = {
    flush: function() {
        var a = this.context.gl,
            b = 4 * this.regIndex;
        switch (this.type) {
            case 35664:
                Kl.uniform2fv(a,
                    this.location, this.regData.subarray(b, b + 2 * this.regCount));
                break;
            case 35665:
                var c = this.location;
                b = this.regData.subarray(b, b + 3 * this.regCount);
                var d = null;
                null != d ? a.uniform3fv(c, b, d, null) : a.uniform3fv(c, b);
                break;
            case 35666:
                c = this.location;
                b = this.regData.subarray(b, b + 4 * this.regCount);
                d = null;
                null != d ? a.uniform4fv(c, b, d, null) : a.uniform4fv(c, b);
                break;
            case 35674:
                Lc.uniformMatrix2fv(a, this.location, !1, this.regData.subarray(b, b + 4 * this.size));
                break;
            case 35675:
                Lc.uniformMatrix3fv(a, this.location, !1, this.regData.subarray(b,
                    b + 9 * this.size));
                break;
            case 35676:
                Lc.uniformMatrix4fv(a, this.location, !1, this.regData.subarray(b, b + 16 * this.size));
                break;
            default:
                c = this.location, b = this.regData.subarray(b, b + 4 * this.regCount), d = null, null != d ? a.uniform4fv(c, b, d, null) : a.uniform4fv(c, b)
        }
    },
    __class__: Bj
};
var Gh = function(a) {
    this.__uniforms = a;
    this.__uniforms.sort(function(a, b) {
        return ya.compare(a.regIndex, b.regIndex)
    });
    var b = 0;
    a = 0;
    for (var c = this.__uniforms; a < c.length;) {
        var d = c[a];
        ++a;
        d.regIndex + d.regCount > b && (b = d.regIndex + d.regCount)
    }
    this.__registerLookup =
        la.toObjectVector(null, b);
    a = 0;
    for (c = this.__uniforms; a < c.length;) {
        d = c[a];
        ++a;
        b = 0;
        for (var f = d.regCount; b < f;) {
            var h = b++;
            this.__registerLookup.set(d.regIndex + h, d)
        }
    }
    this.__anyDirty = this.__allDirty = !0
};
g["openfl.display3D.UniformMap"] = Gh;
Gh.__name__ = "openfl.display3D.UniformMap";
Gh.prototype = {
    flush: function() {
        if (this.__anyDirty) {
            for (var a = 0, b = this.__uniforms; a < b.length;) {
                var c = b[a];
                ++a;
                if (this.__allDirty || c.isDirty) c.flush(), c.isDirty = !1
            }
            this.__anyDirty = this.__allDirty = !1
        }
    },
    markAllDirty: function() {
        this.__anyDirty =
            this.__allDirty = !0
    },
    markDirty: function(a, b) {
        if (!this.__allDirty)
            for (b = a + b, b > this.__registerLookup.get_length() && (b = this.__registerLookup.get_length()); a < b;) {
                var c = this.__registerLookup.get(a);
                null != c ? (this.__anyDirty = c.isDirty = !0, a = c.regIndex + c.regCount) : ++a
            }
    },
    __class__: Gh
};
var Lk = function(a, b, c, d) {
    this.__context = a;
    this.__numVertices = b;
    this.__vertexSize = c;
    a = this.__context.gl;
    this.__id = a.createBuffer();
    this.__stride = 4 * this.__vertexSize;
    this.__usage = 0 == Al.fromString(d) ? a.DYNAMIC_DRAW : a.STATIC_DRAW
};
g["openfl.display3D.VertexBuffer3D"] = Lk;
Lk.__name__ = "openfl.display3D.VertexBuffer3D";
Lk.prototype = {
    uploadFromTypedArray: function(a, b) {
        null != a && (b = this.__context.gl, this.__context.__bindGLArrayBuffer(this.__id), Lc.bufferData(b, b.ARRAY_BUFFER, a, this.__usage))
    },
    uploadFromVector: function(a, b, c) {
        if (null != a) {
            b *= this.__vertexSize;
            var d = c * this.__vertexSize;
            c = b + d;
            var f = this.__tempFloat32Array;
            if (null == this.__tempFloat32Array || this.__tempFloat32Array.length < d) this.__tempFloat32Array = null != d ? new Float32Array(d) :
                null, null != f && this.__tempFloat32Array.set(f);
            for (d = b; d < c;) f = d++, this.__tempFloat32Array[f - b] = a.get(f);
            this.uploadFromTypedArray(ih.toArrayBufferView(this.__tempFloat32Array))
        }
    },
    __class__: Lk
};
var bd = function() {};
g["openfl.display3D._internal.AGALConverter"] = bd;
bd.__name__ = "openfl.display3D._internal.AGALConverter";
bd.prefixFromType = function(a, b) {
    switch (a) {
        case 0:
            return "va";
        case 1:
            return b == Fe.VERTEX ? "vc" : "fc";
        case 2:
            return b == Fe.VERTEX ? "vt" : "ft";
        case 3:
            return "output_";
        case 4:
            return "v";
        case 5:
            return "sampler";
        default:
            throw new Wc("Invalid data!");
    }
};
bd.readUInt64 = function(a) {
    var b = a.readInt();
    a = a.readInt();
    return new Da(a, b)
};
bd.convertToGLSL = function(a, b) {
    a.position = 0;
    a.__endian = 1;
    var c = a.readByte() & 255;
    if (176 == c) return a.readUTF();
    if (160 != c) throw new Wc("Magic value must be 0xA0, may not be AGAL");
    var d = a.readInt();
    if (1 != d) throw new Wc("Version must be 1");
    if (161 != (a.readByte() & 255)) throw new Wc("Shader type ID must be 0xA1");
    c = 0 == (a.readByte() & 255) ? Fe.VERTEX : Fe.FRAGMENT;
    for (var f = new Cj, h = "";;) {
        d = a.position;
        if (!cb.gt(Td.get_length(a), d)) break;
        var k = a.readInt();
        d = a.readUnsignedInt();
        var g = bd.readUInt64(a),
            p = bd.readUInt64(a);
        d = Mg.parse(d, c);
        g = cg.parse(g, c, d.mask);
        var q = cg.parse(p, c, d.mask);
        h += "\t";
        switch (k) {
            case 0:
                h += H.string(d.toGLSL() + " = " + g.toGLSL() + "; // mov");
                f.addDR(d, fa.VECTOR_4);
                f.addSR(g, fa.VECTOR_4);
                break;
            case 1:
                h += H.string(d.toGLSL() + " = " + g.toGLSL() + " + " + q.toGLSL() + "; // add");
                f.addDR(d, fa.VECTOR_4);
                f.addSR(g, fa.VECTOR_4);
                f.addSR(q, fa.VECTOR_4);
                break;
            case 2:
                h += H.string(d.toGLSL() + " = " + g.toGLSL() +
                    " - " + q.toGLSL() + "; // sub");
                f.addDR(d, fa.VECTOR_4);
                f.addSR(g, fa.VECTOR_4);
                f.addSR(q, fa.VECTOR_4);
                break;
            case 3:
                h += H.string(d.toGLSL() + " = " + g.toGLSL() + " * " + q.toGLSL() + "; // mul");
                f.addDR(d, fa.VECTOR_4);
                f.addSR(g, fa.VECTOR_4);
                f.addSR(q, fa.VECTOR_4);
                break;
            case 4:
                h += H.string(d.toGLSL() + " = " + g.toGLSL() + " / " + q.toGLSL() + "; // div");
                f.addDR(d, fa.VECTOR_4);
                f.addSR(g, fa.VECTOR_4);
                f.addSR(q, fa.VECTOR_4);
                break;
            case 5:
                h = -1 < g.toGLSL().indexOf(".") ? h + H.string(d.toGLSL() + " = 1.0 / " + g.toGLSL() + "; // rcp") : h +
                    H.string(d.toGLSL() + " = vec4(1) / " + g.toGLSL() + "; // rcp");
                f.addDR(d, fa.VECTOR_4);
                f.addSR(g, fa.VECTOR_4);
                break;
            case 6:
                h += H.string(d.toGLSL() + " = min(" + g.toGLSL() + ", " + q.toGLSL() + "); // min");
                f.addDR(d, fa.VECTOR_4);
                f.addSR(g, fa.VECTOR_4);
                f.addSR(q, fa.VECTOR_4);
                break;
            case 7:
                h += H.string(d.toGLSL() + " = max(" + g.toGLSL() + ", " + q.toGLSL() + "); // max");
                f.addDR(d, fa.VECTOR_4);
                f.addSR(g, fa.VECTOR_4);
                f.addSR(q, fa.VECTOR_4);
                break;
            case 8:
                h += H.string(d.toGLSL() + " = fract(" + g.toGLSL() + "); // frc");
                f.addDR(d, fa.VECTOR_4);
                f.addSR(g, fa.VECTOR_4);
                break;
            case 9:
                h += H.string(d.toGLSL() + " = sqrt(" + g.toGLSL() + "); // sqrt");
                f.addDR(d, fa.VECTOR_4);
                f.addSR(g, fa.VECTOR_4);
                break;
            case 10:
                h += H.string(d.toGLSL() + " = inversesqrt(" + g.toGLSL() + "); // rsq");
                f.addDR(d, fa.VECTOR_4);
                f.addSR(g, fa.VECTOR_4);
                break;
            case 11:
                h += H.string(d.toGLSL() + " = pow(" + g.toGLSL() + ", " + q.toGLSL() + "); // pow");
                f.addDR(d, fa.VECTOR_4);
                f.addSR(g, fa.VECTOR_4);
                f.addSR(q, fa.VECTOR_4);
                break;
            case 12:
                h += H.string(d.toGLSL() + " = log2(" + g.toGLSL() + "); // log");
                f.addDR(d,
                    fa.VECTOR_4);
                f.addSR(g, fa.VECTOR_4);
                break;
            case 13:
