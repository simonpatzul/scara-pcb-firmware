# Cinemática inversa del SCARA (traducción directa del script de MATLAB). Sin ROS.
# Convención: ángulos medidos desde el eje Y  ->  X = a1 sin(q1) + a2 sin(q1+q2),  Y = a1 cos(q1) + a2 cos(q1+q2)
# Unidades: mm y rad.
import math

a1 = 125.053
a2 = 125.053 + 23


def ik(X, Y, Z):
    """(X, Y) en mm -> [q1, q2] en rad, o None si el punto está fuera de alcance."""
    r1 = math.sqrt(X**2 + Y**2)

    if r1 > a1 + a2 or r1 < abs(a1 - a2):
        return None

    c_phi = (a1**2 + r1**2 - a2**2) / (2 * r1 * a1)
    c_q2 = (r1**2 - a1**2 - a2**2) / (2 * a1 * a2)

    c_phi = max(-1.0, min(1.0, c_phi))   # evita que un error de redondeo saque al acos de [-1, 1]
    c_q2 = max(-1.0, min(1.0, c_q2))

    phi1 = math.acos(c_phi)
    q1 = math.atan2(X, Y) - phi1
    q2 = math.acos(c_q2)
    p=Z
    return [q1, q2, p]


def fk(q1, q2, Z):
    """Directa, para verificar: [q1, q2, Z] en rad -> (X, Y) en mm."""
    X = a1 * math.sin(q1) + a2 * math.sin(q1 + q2)
    Y = a1 * math.cos(q1) + a2 * math.cos(q1 + q2)
    p = Z
    return X, Y, p
