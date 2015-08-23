'use strict';

requirejs.config({
	//baseUrl: 'lib',
	paths: {
		'jquery': 'https://cdnjs.cloudflare.com/ajax/libs/jquery/2.1.4/jquery.min',
		'bootstrap': 'https://maxcdn.bootstrapcdn.com/bootstrap/3.2.0/js/bootstrap.min',
		'domReady': 'https://cdnjs.cloudflare.com/ajax/libs/require-domReady/2.0.1/domReady.min',

		'moment': 'https://cdnjs.cloudflare.com/ajax/libs/moment.js/2.10.6/moment.min',

		//'angular': 'https://cdnjs.cloudflare.com/ajax/libs/angular.js/1.4.4/angular.min',
		'angular': 'https://cdnjs.cloudflare.com/ajax/libs/angular.js/1.4.4/angular',
		'angular-resource': 'https://cdnjs.cloudflare.com/ajax/libs/angular.js/1.4.4/angular-resource.min',
		'angular-ui-router': 'https://cdnjs.cloudflare.com/ajax/libs/angular-ui-router/0.2.15/angular-ui-router.min',
	},
	'shim': {
		'bootstrap':         { 'deps': ['jquery'] },
		'angular':           { 'deps': ['jquery'], 'exports': 'angular' },
		'angular-resource':  { 'deps': ['angular'] },
		'angular-ui-router': { 'deps': ['angular'] },
	}
});

requirejs(['domReady', 'angular', 'app'], function (domReady, angular) {
	domReady(function() {
		angular.bootstrap(document, ['httprcDemo']);
	});
});
