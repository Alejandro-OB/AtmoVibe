<?php
header('Content-Type: application/json');

// Configuración de la conexión a la base de datos
$servername = "localhost"; // Cambia esto si tu servidor es diferente
$username = "root";        // Cambia por el usuario de la base de datos
$password = "";            // Cambia por la contraseña del usuario de la base de datos
$dbname = "atmovibe";      // Nombre de la base de datos

// Crear conexión
try {
    $conn = new PDO("mysql:host=$servername;dbname=$dbname", $username, $password);
    // Establecer el modo de error PDO a excepción
    $conn->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
} catch (PDOException $e) {
    echo json_encode(array("status" => "error", "message" => "Connection failed: " . $e->getMessage()));
    exit();
}

// Verificar si se enviaron datos con el método POST
if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    // Obtener el cuerpo de la solicitud
    $inputJSON = file_get_contents('php://input');
    $input = json_decode($inputJSON, TRUE); // Decodificar el cuerpo JSON

    // Verificar si existen los parámetros de usuario y contraseña
    if (isset($input['username']) && isset($input['password'])) {
        $username = $input['username'];
        $password = $input['password'];

        // Consultar en la base de datos si existe el usuario
        $stmt = $conn->prepare("SELECT * FROM usuarios WHERE username = :username");
        $stmt->bindParam(':username', $username);
        $stmt->execute();
        $user = $stmt->fetch(PDO::FETCH_ASSOC);

        if ($user) {
            // Verificar la contraseña
            if ($password == $user['password']) { // Aquí puedes usar `password_verify($password, $user['password'])` si las contraseñas están encriptadas
                echo json_encode(array("status" => "success", "message" => "Login successful"));
            } else {
                echo json_encode(array("status" => "error", "message" => "Incorrect password"));
            }
        } else {
            echo json_encode(array("status" => "error", "message" => "User not found"));
        }
    } else {
        echo json_encode(array("status" => "error", "message" => "Username and password are required"));
    }
} else {
    echo json_encode(array("status" => "error", "message" => "Invalid request method"));
}

$conn = null; // Cerrar conexión
?>
