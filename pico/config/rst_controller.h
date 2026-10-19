#pragma once
// Controlador RST de Landau, en forma incremental con anti-windup.
// Trabaja en unidades de la identificación: ángulo del MOTOR en rad, entrada u en voltios.
//
// Ley de control:  S'(q^-1) * du(k) = T(q^-1) * y*(k) - R(q^-1) * y(k)
//                  u(k) = u(k-1) + du(k)         (S = S' * (1 - q^-1): el integrador)
// y*(k) es la referencia suavizada por el modelo  Am y*(k) = Bm r(k-1).

// Coeficientes de un eje (salen del diseño: control/rst_diseno.md)
struct RstParams {
    double R[5];    // R(q^-1)
    double Sp[4];   // S'(q^-1), el primer coeficiente es 1
    double T[8];    // T(q^-1)
    double Am[3];   // denominador del modelo de referencia, el primer coeficiente es 1
    double Bm;      // numerador del modelo de referencia
};

class RstController {
public:
    RstController(const RstParams& p, float u_max) : p_(p), u_max_(u_max) {}

    void reset(float y0);                  // arranca "en reposo" en el ángulo y0 (sin salto en u)
    float step(float ref, float y);        // un ciclo: devuelve u en voltios, ya saturado

private:
    const RstParams& p_;
    double u_max_;
    double y_[5] = {};    // y(k), y(k-1), ... y(k-4)
    double ys_[8] = {};   // y*(k), y*(k-1), ... y*(k-7)
    double du_[3] = {};   // du(k-1), du(k-2), du(k-3)  (los incrementos ya saturados)
    double r_prev_ = 0.0; // r(k-1)
    double u_ = 0.0;      // u(k-1), ya saturado
};
