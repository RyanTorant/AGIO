# Resultados
Se realizaron diversos experimentos para medir el desempeño del algoritmo. A continuación, se muestran brevemente los resultados obtenidos en algunos de ellos.

## Carnívoros y Herbívoros
El siguiente video muestra una simulación de individuos carnívoros, en rojo, y herbívoros, en verde claro, evolucionados con AGIO. Las manchas verdes distribuidas en el entorno corresponden a comida de la que se pueden alimentar los herbívoros. Como se puede ver, los comportamientos son diversos y acordes al problema, donde los carnívoros cazan a los herbívoros y los herbívoros se alimentan de la comida del entorno. Por simplicidad, los individuos solo tienen sensores que indican dónde se encuentra la comida más cercana, por lo que no pueden saber donde se encuentran los demás individuos. Por ejemplo, los herbívoros no tienen la información para poder escapar de los carnívoros.
<p style="text-align:center">
<iframe width="560" height="315" src="https://www.youtube.com/embed/xPl5bAr2WiA" frameborder="0" allow="accelerometer; autoplay; encrypted-media; gyroscope; picture-in-picture" allowfullscreen></iframe>
</p>

## Interrelaciones entre especies
Para medir qué tan relacionadas se encuentran las especies encontradas por AGIO en un escenario en particular, se realizaron varias simulaciones para obtener la media y desviación de la distribución de fitness de cada especie (que resulta seguir una distribución normal), y luego se realizan varias simulación, retirando de la simulación una de las especies y dejando las demás. Si alguna especie se encuentra relacionada con la que fue retirada, su fitness tiene que verse afectado significativamente. 

El grafo siguiente muestra las relaciones encontradas entre las diversas especies. Los rombos representan omnívoros, las elipses herbívoros y los cuadrados carnívoros. En rojo se marcan las especies que fallaron en evolucionar (su fitness fue siempre 0). La dirección de las flechas se interpreta de la siguiente manera : Si A → B, entonces el fitness de la especie A se ve afectado cuando la especie B es retirada de la simulación.

<p style="text-align:center">
<img src="interrelaciones.png" alt="Grafo de interrelaciones entre especies" width="600"/>
</p>

Se encontraron 10 relaciones entre especies para el escenario analizado, junto con cadenas de dependencias, como la formadas entre las especies 4, 8 y 5, por ejemplo. 

## AGIO vs Jugador
Como AGIO está enfocado al ámbito de los videojuegos, es crucial saber qué tan adecuados son los comportamientos de los individuos cuando se introduce un jugador humano. Para esto, se elige al azar un individuo de una de de las especies evolucionadas y se le da el control del individuo al jugador, para que decida que acción realizar en cada paso. Al jugador se le muestran los valores de los sensores del individuo y con esa información tiene que decidir qué acción realizar.

La siguiente gráfica muestra en negro el fitness de los individuos de la misma especie que el individuo controlado por el jugador y en azul el fitness del individuo controlado por el jugador. Como se puede ver, el comportamiento de los individuos evolucionados por AGIO es comparable al del jugador. Incluso AGIO logra superar al jugador en las partes finales de la simulación
<p style="text-align:center">
<img src="vsjugador.png" alt="Fitness de AGIO y del jugador" width="475"/>
</p>