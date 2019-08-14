# Propuesta

AGIO genera especies de individuos y para cada individuo genera un comportamiento. Una especie se define como un conjunto de individuos que pueden cruzarse entre sí en el proceso evolutivo y que comparten una misma morfología. Cada especie tiene su propia población independiente de NEAT, por lo que en AGIO se realizan _N_ procesos evolutivos independientes, donde _N_ es la cantidad de especies evolucionadas simultáneamente. 

<p style="text-align:right">
<img align="right" src="morfologia.png" alt="estructura de los individuos" width="400"/> </p>  La estructura de los individuos, que se muestra en la imágen, permite a los diseñadores tener un control directo sobre cómo se pueden formar los individuos, qué acciones pueden realizar y qué información tienen del entorno. Adicionalmente, los parámetros de los individuos son valores numéricos que permiten mayor poder de expresión (un parámetro podría ser _fuerza_, asociado a una acción de atacar).

Cada individuo tiene su propia red neuronal para la toma de decisiones. La red toma como entrada los valores de los sensores que el individuo posee y da como salida la probabilidad de realizar cada una de las acciones que el individuo puede realizar. La acción a realizar por el individuo es elegida aleatoriamente, siguiendo la distribución de probabilidad determinada por la red.